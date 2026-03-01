// SPDX-License-Identifier: MIT
package io.conduit;

import java.lang.foreign.*;

/**
 * Panama FFI (JDK 21+) implementation of {@link NativeCodecBinding}.
 * <p>
 * Wraps the existing {@link CodecBindings} MethodHandles and translates
 * between Java types and native memory.
 */
public final class PanamaCodecBinding implements NativeCodecBinding {

    // conduit_decoded_msg_t layout: { uint64_t type_id; char* type_name; uint8_t* data; size_t data_len }
    private static final long MSG_STRUCT_SIZE = 32;
    private static final long MSG_TYPE_ID_OFFSET = 0;
    private static final long MSG_TYPE_NAME_OFFSET = 8;
    private static final long MSG_DATA_OFFSET = 16;
    private static final long MSG_DATA_LEN_OFFSET = 24;

    // conduit_encode_result_t layout: { uint8_t* data; size_t data_len }
    private static final long ENCODE_RESULT_SIZE = 16;
    private static final long ENCODE_DATA_OFFSET = 0;
    private static final long ENCODE_DATA_LEN_OFFSET = 8;

    // conduit_frame_t layout: { uint8_t* data; size_t data_len }
    private static final long FRAME_STRUCT_SIZE = 16;
    private static final long FRAME_DATA_OFFSET = 0;
    private static final long FRAME_DATA_LEN_OFFSET = 8;

    // ================================================================
    // Session lifecycle
    // ================================================================

    @Override
    public long sessionCreate(String sessionType) {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String(sessionType);
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            return s.address();
        } catch (Throwable e) {
            throw new RuntimeException("sessionCreate failed", e);
        }
    }

    @Override
    public void sessionDestroy(long session) {
        try {
            CodecBindings.conduit_session_destroy.invokeExact(MemorySegment.ofAddress(session));
        } catch (Throwable e) {
            throw new RuntimeException("sessionDestroy failed", e);
        }
    }

    @Override
    public void sessionReset(long session) {
        try {
            CodecBindings.conduit_session_reset.invokeExact(MemorySegment.ofAddress(session));
        } catch (Throwable e) {
            throw new RuntimeException("sessionReset failed", e);
        }
    }

    // ================================================================
    // Decode
    // ================================================================

    @Override
    public DecodedMessage[] decodeFrame(long session, byte[] data) {
        try (var localArena = Arena.ofConfined()) {
            var dataSeg = localArena.allocateArray(ValueLayout.JAVA_BYTE, data);
            var outMsgs = localArena.allocate(ValueLayout.ADDRESS);
            var outCount = localArena.allocate(ValueLayout.JAVA_LONG);

            int err = (int) CodecBindings.conduit_decode_frame.invokeExact(
                MemorySegment.ofAddress(session), dataSeg, (long) data.length, outMsgs, outCount);
            if (err != 0) return new DecodedMessage[0];

            long count = outCount.get(ValueLayout.JAVA_LONG, 0);
            if (count == 0) return new DecodedMessage[0];

            MemorySegment msgsPtr = outMsgs.get(ValueLayout.ADDRESS, 0);
            MemorySegment msgs = msgsPtr.reinterpret(count * MSG_STRUCT_SIZE);

            DecodedMessage[] result = new DecodedMessage[(int) count];
            for (int i = 0; i < count; i++) {
                long offset = i * MSG_STRUCT_SIZE;
                long typeId = msgs.get(ValueLayout.JAVA_LONG, offset + MSG_TYPE_ID_OFFSET);

                MemorySegment namePtr = msgs.get(ValueLayout.ADDRESS, offset + MSG_TYPE_NAME_OFFSET);
                String typeName = namePtr != MemorySegment.NULL
                    ? namePtr.reinterpret(256).getUtf8String(0) : "";

                long dataLen = msgs.get(ValueLayout.JAVA_LONG, offset + MSG_DATA_LEN_OFFSET);
                byte[] msgData;
                if (dataLen > 0) {
                    MemorySegment dataPtr = msgs.get(ValueLayout.ADDRESS, offset + MSG_DATA_OFFSET);
                    msgData = dataPtr.reinterpret(dataLen).toArray(ValueLayout.JAVA_BYTE);
                } else {
                    msgData = new byte[0];
                }

                result[i] = new DecodedMessage(typeId, typeName, msgData);
            }

            CodecBindings.conduit_free_decoded_msgs.invokeExact(msgsPtr, count);
            return result;
        } catch (Throwable e) {
            throw new RuntimeException("decodeFrame failed", e);
        }
    }

    // ================================================================
    // Encode
    // ================================================================

    @Override
    public byte[] encodeMessage(long session, long typeId, byte[] payload) {
        try (var localArena = Arena.ofConfined()) {
            var payloadSeg = localArena.allocateArray(ValueLayout.JAVA_BYTE, payload);
            var encodeResult = localArena.allocate(ENCODE_RESULT_SIZE);

            int err = (int) CodecBindings.conduit_encode_message.invokeExact(
                MemorySegment.ofAddress(session), typeId,
                payloadSeg, (long) payload.length, encodeResult);
            if (err != 0) return null;

            long encLen = encodeResult.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
            MemorySegment encData = encodeResult.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
            byte[] result = encData.reinterpret(encLen).toArray(ValueLayout.JAVA_BYTE);

            CodecBindings.conduit_free_encode_result.invokeExact(encodeResult);
            return result;
        } catch (Throwable e) {
            throw new RuntimeException("encodeMessage failed", e);
        }
    }

    @Override
    public byte[] encodeBatch(long session, long typeId, byte[][] payloads) {
        try (var localArena = Arena.ofConfined()) {
            int count = payloads.length;
            var ptrs = localArena.allocate(
                ValueLayout.ADDRESS.byteSize() * count,
                ValueLayout.ADDRESS.byteAlignment());
            var lens = localArena.allocate(
                ValueLayout.JAVA_LONG.byteSize() * count,
                ValueLayout.JAVA_LONG.byteAlignment());

            for (int i = 0; i < count; i++) {
                var buf = localArena.allocateArray(ValueLayout.JAVA_BYTE, payloads[i]);
                ptrs.setAtIndex(ValueLayout.ADDRESS, i, buf);
                lens.setAtIndex(ValueLayout.JAVA_LONG, i, (long) payloads[i].length);
            }

            var encodeResult = localArena.allocate(ENCODE_RESULT_SIZE);
            int err = (int) CodecBindings.conduit_encode_batch.invokeExact(
                MemorySegment.ofAddress(session), typeId,
                ptrs, lens, (long) count, encodeResult);
            if (err != 0) return null;

            long encLen = encodeResult.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
            MemorySegment encData = encodeResult.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
            byte[] result = encData.reinterpret(encLen).toArray(ValueLayout.JAVA_BYTE);

            CodecBindings.conduit_free_encode_result.invokeExact(encodeResult);
            return result;
        } catch (Throwable e) {
            throw new RuntimeException("encodeBatch failed", e);
        }
    }

    // ================================================================
    // Introspection
    // ================================================================

    @Override
    public String sessionTypeName(long session, long typeId) {
        try {
            MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_type_name
                .invokeExact(MemorySegment.ofAddress(session), typeId);
            if (namePtr == MemorySegment.NULL) return "";
            return namePtr.reinterpret(256).getUtf8String(0);
        } catch (Throwable e) {
            throw new RuntimeException("sessionTypeName failed", e);
        }
    }

    @Override
    public long sessionLeafTypeCount(long session) {
        try {
            return (long) CodecBindings.conduit_session_leaf_type_count
                .invokeExact(MemorySegment.ofAddress(session));
        } catch (Throwable e) {
            throw new RuntimeException("sessionLeafTypeCount failed", e);
        }
    }

    @Override
    public long[] sessionLeafTypeIds(long session) {
        try {
            long count = sessionLeafTypeCount(session);
            MemorySegment idsPtr = (MemorySegment) CodecBindings.conduit_session_leaf_type_ids
                .invokeExact(MemorySegment.ofAddress(session));
            if (idsPtr == MemorySegment.NULL) return new long[0];

            MemorySegment ids = idsPtr.reinterpret(count * 8);
            long[] result = new long[(int) count];
            for (int i = 0; i < count; i++) {
                result[i] = ids.get(ValueLayout.JAVA_LONG, (long) i * 8);
            }
            return result;
        } catch (Throwable e) {
            throw new RuntimeException("sessionLeafTypeIds failed", e);
        }
    }

    @Override
    public boolean sessionIsReceiveOnly(long session, long typeId) {
        try {
            return (int) CodecBindings.conduit_session_is_receive_only
                .invokeExact(MemorySegment.ofAddress(session), typeId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("sessionIsReceiveOnly failed", e);
        }
    }

    @Override
    public String sessionProtocolName(long session) {
        try {
            MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_protocol_name
                .invokeExact(MemorySegment.ofAddress(session));
            if (namePtr == MemorySegment.NULL) return "";
            return namePtr.reinterpret(256).getUtf8String(0);
        } catch (Throwable e) {
            throw new RuntimeException("sessionProtocolName failed", e);
        }
    }

    // ================================================================
    // Human-readable formatting
    // ================================================================

    @Override
    public String formatMessage(long session, long typeId, byte[] payload) {
        try (var localArena = Arena.ofConfined()) {
            var payloadSeg = localArena.allocateArray(ValueLayout.JAVA_BYTE, payload);
            int bufLen = 4096;
            var buf = localArena.allocate(bufLen);
            var outWritten = localArena.allocate(ValueLayout.JAVA_LONG);

            int err = (int) CodecBindings.conduit_format_message.invokeExact(
                MemorySegment.ofAddress(session), typeId,
                payloadSeg, (long) payload.length,
                buf, (long) bufLen, outWritten);
            if (err != 0) return null;

            long written = outWritten.get(ValueLayout.JAVA_LONG, 0);
            return buf.reinterpret(written).getUtf8String(0);
        } catch (Throwable e) {
            throw new RuntimeException("formatMessage failed", e);
        }
    }

    // ================================================================
    // Framing
    // ================================================================

    @Override
    public long framerCreate(long session) {
        try {
            MemorySegment framer = (MemorySegment) CodecBindings.conduit_framer_create
                .invokeExact(MemorySegment.ofAddress(session));
            return framer.address();
        } catch (Throwable e) {
            throw new RuntimeException("framerCreate failed", e);
        }
    }

    @Override
    public void framerDestroy(long framer) {
        try {
            CodecBindings.conduit_framer_destroy.invokeExact(MemorySegment.ofAddress(framer));
        } catch (Throwable e) {
            throw new RuntimeException("framerDestroy failed", e);
        }
    }

    @Override
    public byte[][] framerFeed(long framer, byte[] data) {
        try (var localArena = Arena.ofConfined()) {
            var dataSeg = localArena.allocateArray(ValueLayout.JAVA_BYTE, data);
            var outFrames = localArena.allocate(ValueLayout.ADDRESS);
            var outCount = localArena.allocate(ValueLayout.JAVA_LONG);

            int err = (int) CodecBindings.conduit_framer_feed.invokeExact(
                MemorySegment.ofAddress(framer), dataSeg, (long) data.length, outFrames, outCount);
            if (err != 0) return new byte[0][];

            long count = outCount.get(ValueLayout.JAVA_LONG, 0);
            if (count == 0) return new byte[0][];

            MemorySegment framesPtr = outFrames.get(ValueLayout.ADDRESS, 0);
            MemorySegment frames = framesPtr.reinterpret(count * FRAME_STRUCT_SIZE);

            byte[][] result = new byte[(int) count][];
            for (int i = 0; i < count; i++) {
                long offset = i * FRAME_STRUCT_SIZE;
                MemorySegment frameDataPtr = frames.get(ValueLayout.ADDRESS, offset + FRAME_DATA_OFFSET);
                long frameDataLen = frames.get(ValueLayout.JAVA_LONG, offset + FRAME_DATA_LEN_OFFSET);
                result[i] = frameDataPtr.reinterpret(frameDataLen).toArray(ValueLayout.JAVA_BYTE);
            }

            CodecBindings.conduit_free_frames.invokeExact(framesPtr, count);
            return result;
        } catch (Throwable e) {
            throw new RuntimeException("framerFeed failed", e);
        }
    }

    // ================================================================
    // Version
    // ================================================================

    @Override
    public String codecVersion() {
        try {
            MemorySegment versionPtr = (MemorySegment) CodecBindings.conduit_codec_version.invokeExact();
            if (versionPtr == MemorySegment.NULL) return "";
            return versionPtr.reinterpret(256).getUtf8String(0);
        } catch (Throwable e) {
            throw new RuntimeException("codecVersion failed", e);
        }
    }

    @Override
    public void close() {
        // No persistent resources
    }
}
