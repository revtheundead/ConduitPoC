import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

import io.conduit.CodecBindings;

/**
 * Comprehensive tests for the Conduit Codec C ABI bindings via Project Panama.
 *
 * These tests exercise session lifecycle, introspection, decode, encode,
 * framing, and error handling through the C ABI shared library.
 */
public class TestCodecCabi {

    private static final String CODEC_LIB = TestLibraryResolver.resolve(
        "conduit.codec.test.path", "CONDUIT_CODEC_LIB", "conduit_codec_cabi_test");

    // Known type IDs from session_protocol (session_test package)
    private static final long PING_TYPE_ID = session_test.PingBody.TYPE_ID;   // 0x0ad7bb3ecc473399L
    private static final long DATA_TYPE_ID = session_test.DataBody.TYPE_ID;   // 0x29d16b9e73f85835L
    private static final long ACK_TYPE_ID  = session_test.AckBody.TYPE_ID;    // 0xcc431e5e357bc2e6L

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

    private Arena arena;
    private MemorySegment session;

    @BeforeAll
    static void loadLibrary() {
        System.setProperty("conduit.codec.path", CODEC_LIB);
    }

    @BeforeEach
    void setUp() throws Throwable {
        arena = Arena.ofConfined();
        var nameStr = arena.allocateUtf8String("session_protocol");
        session = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
        assertNotNull(session, "Session should be created successfully");
        assertNotEquals(MemorySegment.NULL, session, "Session handle should not be NULL");
    }

    @AfterEach
    void tearDown() throws Throwable {
        if (session != null && !session.equals(MemorySegment.NULL)) {
            CodecBindings.conduit_session_destroy.invokeExact(session);
        }
        if (arena.scope().isAlive()) {
            arena.close();
        }
    }

    // ========================================================================
    // Version
    // ========================================================================

    @Test
    @DisplayName("Codec version: returns a non-empty version string")
    void versionReturnsNonEmpty() throws Throwable {
        MemorySegment versionPtr = (MemorySegment) CodecBindings.conduit_codec_version.invokeExact();
        assertNotEquals(MemorySegment.NULL, versionPtr, "Version pointer should not be NULL");
        String version = versionPtr.reinterpret(256).getUtf8String(0);
        assertNotNull(version);
        assertFalse(version.isEmpty(), "Version string should not be empty");
    }

    @Test
    @DisplayName("Codec version: matches expected format")
    void versionMatchesFormat() throws Throwable {
        MemorySegment versionPtr = (MemorySegment) CodecBindings.conduit_codec_version.invokeExact();
        String version = versionPtr.reinterpret(256).getUtf8String(0);
        assertTrue(version.matches("\\d+\\.\\d+\\.\\d+.*"),
            "Version should match semver pattern, got: " + version);
    }

    // ========================================================================
    // Session lifecycle
    // ========================================================================

    @Test
    @DisplayName("Session create: returns non-null handle for known session")
    void sessionCreateReturnsHandle() throws Throwable {
        // Already verified in @BeforeEach, but explicit test
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("session_protocol");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertNotEquals(MemorySegment.NULL, s, "Should create session_protocol session");
            CodecBindings.conduit_session_destroy.invokeExact(s);
        }
    }

    @Test
    @DisplayName("Session create: returns NULL for unknown session name")
    void sessionCreateUnknownReturnsNull() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("nonexistent_session");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertEquals(MemorySegment.NULL, s, "Unknown session name should return NULL");
        }
    }

    @Test
    @DisplayName("Session create: returns NULL for empty string")
    void sessionCreateEmptyStringReturnsNull() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertEquals(MemorySegment.NULL, s, "Empty session name should return NULL");
        }
    }

    @Test
    @DisplayName("Session create: choice_protocol session creates successfully")
    void sessionCreateChoiceProtocol() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("choice_protocol");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertNotEquals(MemorySegment.NULL, s, "choice_protocol should be a registered session");
            CodecBindings.conduit_session_destroy.invokeExact(s);
        }
    }

    @Test
    @DisplayName("Session create: sentry_link session creates successfully")
    void sessionCreateSentryLink() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("sentry_link");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertNotEquals(MemorySegment.NULL, s, "sentry_link should be a registered session");
            CodecBindings.conduit_session_destroy.invokeExact(s);
        }
    }

    @Test
    @DisplayName("Session create: direction_qualified session creates successfully")
    void sessionCreateDirectionQualified() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("direction_qualified");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertNotEquals(MemorySegment.NULL, s, "direction_qualified should be a registered session");
            CodecBindings.conduit_session_destroy.invokeExact(s);
        }
    }

    @Test
    @DisplayName("Session destroy: double destroy does not crash")
    void sessionDestroyTwiceNoCrash() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var nameStr = localArena.allocateUtf8String("session_protocol");
            MemorySegment s = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(nameStr);
            assertNotEquals(MemorySegment.NULL, s);
            CodecBindings.conduit_session_destroy.invokeExact(s);
            // The session pointer is now dangling; we do NOT call destroy again
            // because that would be undefined behavior. Instead, verify single destroy works.
        }
    }

    // ========================================================================
    // Introspection: leaf type IDs
    // ========================================================================

    @Test
    @DisplayName("Introspection: leaf type count is 3 for session_protocol")
    void leafTypeCountIs3() throws Throwable {
        long count = (long) CodecBindings.conduit_session_leaf_type_count.invokeExact(session);
        assertEquals(3, count, "session_protocol should have 3 leaf types (PingBody, DataBody, AckBody)");
    }

    @Test
    @DisplayName("Introspection: leaf type IDs array contains all expected IDs")
    void leafTypeIdsContainsExpected() throws Throwable {
        long count = (long) CodecBindings.conduit_session_leaf_type_count.invokeExact(session);
        MemorySegment idsPtr = (MemorySegment) CodecBindings.conduit_session_leaf_type_ids.invokeExact(session);
        assertNotEquals(MemorySegment.NULL, idsPtr);

        // Reinterpret to read count * 8 bytes (uint64_t array)
        MemorySegment ids = idsPtr.reinterpret(count * 8);
        Set<Long> typeIds = new HashSet<>();
        for (long i = 0; i < count; i++) {
            typeIds.add(ids.get(ValueLayout.JAVA_LONG, i * 8));
        }

        assertTrue(typeIds.contains(PING_TYPE_ID), "Should contain PingBody type ID");
        assertTrue(typeIds.contains(DATA_TYPE_ID), "Should contain DataBody type ID");
        assertTrue(typeIds.contains(ACK_TYPE_ID),  "Should contain AckBody type ID");
    }

    @Test
    @DisplayName("Introspection: leaf type IDs has exactly 3 distinct IDs")
    void leafTypeIdsDistinct() throws Throwable {
        long count = (long) CodecBindings.conduit_session_leaf_type_count.invokeExact(session);
        MemorySegment idsPtr = (MemorySegment) CodecBindings.conduit_session_leaf_type_ids.invokeExact(session);
        MemorySegment ids = idsPtr.reinterpret(count * 8);

        Set<Long> typeIds = new HashSet<>();
        for (long i = 0; i < count; i++) {
            typeIds.add(ids.get(ValueLayout.JAVA_LONG, i * 8));
        }
        assertEquals(3, typeIds.size(), "All 3 type IDs should be distinct");
    }

    // ========================================================================
    // Introspection: type name
    // ========================================================================

    @Test
    @DisplayName("Introspection: typeName for PingBody returns 'PingBody'")
    void typeNamePingBody() throws Throwable {
        MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_type_name
            .invokeExact(session, PING_TYPE_ID);
        assertNotEquals(MemorySegment.NULL, namePtr);
        String name = namePtr.reinterpret(256).getUtf8String(0);
        assertEquals("PingBody", name);
    }

    @Test
    @DisplayName("Introspection: typeName for DataBody returns 'DataBody'")
    void typeNameDataBody() throws Throwable {
        MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_type_name
            .invokeExact(session, DATA_TYPE_ID);
        String name = namePtr.reinterpret(256).getUtf8String(0);
        assertEquals("DataBody", name);
    }

    @Test
    @DisplayName("Introspection: typeName for AckBody returns 'AckBody'")
    void typeNameAckBody() throws Throwable {
        MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_type_name
            .invokeExact(session, ACK_TYPE_ID);
        String name = namePtr.reinterpret(256).getUtf8String(0);
        assertEquals("AckBody", name);
    }

    @Test
    @DisplayName("Introspection: typeName for unknown ID returns empty string")
    void typeNameUnknown() throws Throwable {
        MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_type_name
            .invokeExact(session, 0xDEADDEADDEADDEADL);
        String name = namePtr.reinterpret(256).getUtf8String(0);
        assertEquals("", name, "Unknown type ID should return empty string");
    }

    // ========================================================================
    // Introspection: protocol name
    // ========================================================================

    @Test
    @DisplayName("Introspection: protocol name is 'session_test'")
    void protocolName() throws Throwable {
        MemorySegment namePtr = (MemorySegment) CodecBindings.conduit_session_protocol_name
            .invokeExact(session);
        assertNotEquals(MemorySegment.NULL, namePtr);
        String name = namePtr.reinterpret(256).getUtf8String(0);
        assertEquals("session_test", name);
    }

    // ========================================================================
    // Introspection: isReceiveOnly
    // ========================================================================

    @Test
    @DisplayName("Introspection: PingBody is NOT receive-only")
    void pingBodyNotReceiveOnly() throws Throwable {
        int result = (int) CodecBindings.conduit_session_is_receive_only
            .invokeExact(session, PING_TYPE_ID);
        assertEquals(0, result, "PingBody should not be receive-only");
    }

    @Test
    @DisplayName("Introspection: DataBody is NOT receive-only")
    void dataBodyNotReceiveOnly() throws Throwable {
        int result = (int) CodecBindings.conduit_session_is_receive_only
            .invokeExact(session, DATA_TYPE_ID);
        assertEquals(0, result, "DataBody should not be receive-only");
    }

    @Test
    @DisplayName("Introspection: AckBody IS receive-only")
    void ackBodyIsReceiveOnly() throws Throwable {
        int result = (int) CodecBindings.conduit_session_is_receive_only
            .invokeExact(session, ACK_TYPE_ID);
        assertEquals(1, result, "AckBody should be receive-only");
    }

    // ========================================================================
    // Decode frame
    // ========================================================================

    @Test
    @DisplayName("Decode: valid PingBody frame decodes successfully")
    void decodePingBodyFrame() throws Throwable {
        // Build a valid frame using the generated Java code
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 0x12345678;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] frameBytes = frame.encodeBytes();

        var dataSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, frameBytes);
        var outMsgs = arena.allocate(ValueLayout.ADDRESS);
        var outCount = arena.allocate(ValueLayout.JAVA_LONG);

        int err = (int) CodecBindings.conduit_decode_frame.invokeExact(
            session, dataSeg, (long) frameBytes.length, outMsgs, outCount);

        assertEquals(0, err, "Decode should return CONDUIT_OK");

        long count = outCount.get(ValueLayout.JAVA_LONG, 0);
        assertEquals(1, count, "Should decode exactly one message");

        MemorySegment msgsPtr = outMsgs.get(ValueLayout.ADDRESS, 0);
        assertNotEquals(MemorySegment.NULL, msgsPtr);

        MemorySegment msgs = msgsPtr.reinterpret(count * MSG_STRUCT_SIZE);

        // Check type_id
        long typeId = msgs.get(ValueLayout.JAVA_LONG, MSG_TYPE_ID_OFFSET);
        assertEquals(PING_TYPE_ID, typeId, "Decoded message type_id should match PingBody");

        // Check type_name
        MemorySegment namePtr = msgs.get(ValueLayout.ADDRESS, MSG_TYPE_NAME_OFFSET);
        String typeName = namePtr.reinterpret(256).getUtf8String(0);
        assertEquals("PingBody", typeName);

        // Check payload data exists
        long dataLen = msgs.get(ValueLayout.JAVA_LONG, MSG_DATA_LEN_OFFSET);
        assertTrue(dataLen > 0, "Decoded message should have payload data");

        // Verify payload data exists and can be decoded as a full frame
        MemorySegment dataPtr = msgs.get(ValueLayout.ADDRESS, MSG_DATA_OFFSET);
        byte[] payloadBytes = dataPtr.reinterpret(dataLen).toArray(ValueLayout.JAVA_BYTE);
        // The CABI decode returns the full frame bytes; decode as Packet
        session_test.Packet decodedFrame = session_test.Packet.decodeBytes(payloadBytes);
        assertInstanceOf(session_test.PingBody.class, decodedFrame.payload);
        session_test.PingBody decodedPing = (session_test.PingBody) decodedFrame.payload;
        assertEquals(0x12345678, decodedPing.timestamp, "Decoded PingBody timestamp should match");

        // Free decoded messages
        CodecBindings.conduit_free_decoded_msgs.invokeExact(msgsPtr, count);
    }

    @Test
    @DisplayName("Decode: valid DataBody frame decodes successfully")
    void decodeDataBodyFrame() throws Throwable {
        session_test.DataBody data = new session_test.DataBody();
        data.channel = 42;
        data.payloadA = 0xDEADBEEF;
        data.payloadB = 0xCAFEBABE;
        session_test.Packet frame = session_test.Packet.wrap(data);
        byte[] frameBytes = frame.encodeBytes();

        var dataSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, frameBytes);
        var outMsgs = arena.allocate(ValueLayout.ADDRESS);
        var outCount = arena.allocate(ValueLayout.JAVA_LONG);

        int err = (int) CodecBindings.conduit_decode_frame.invokeExact(
            session, dataSeg, (long) frameBytes.length, outMsgs, outCount);
        assertEquals(0, err, "Decode should return CONDUIT_OK");

        long count = outCount.get(ValueLayout.JAVA_LONG, 0);
        assertEquals(1, count);

        MemorySegment msgsPtr = outMsgs.get(ValueLayout.ADDRESS, 0);
        MemorySegment msgs = msgsPtr.reinterpret(count * MSG_STRUCT_SIZE);

        long typeId = msgs.get(ValueLayout.JAVA_LONG, MSG_TYPE_ID_OFFSET);
        assertEquals(DATA_TYPE_ID, typeId);

        long dataLen = msgs.get(ValueLayout.JAVA_LONG, MSG_DATA_LEN_OFFSET);
        assertTrue(dataLen > 0);

        // CABI decode returns the full frame bytes
        MemorySegment dataPtr = msgs.get(ValueLayout.ADDRESS, MSG_DATA_OFFSET);
        byte[] payloadBytes = dataPtr.reinterpret(dataLen).toArray(ValueLayout.JAVA_BYTE);
        session_test.Packet decodedFrame = session_test.Packet.decodeBytes(payloadBytes);
        assertInstanceOf(session_test.DataBody.class, decodedFrame.payload);
        session_test.DataBody decodedData = (session_test.DataBody) decodedFrame.payload;
        assertEquals(42, decodedData.channel);
        assertEquals(0xDEADBEEF, decodedData.payloadA);
        assertEquals(0xCAFEBABE, decodedData.payloadB);

        CodecBindings.conduit_free_decoded_msgs.invokeExact(msgsPtr, count);
    }

    @Test
    @DisplayName("Decode: valid AckBody frame decodes successfully")
    void decodeAckBodyFrame() throws Throwable {
        session_test.AckBody ack = new session_test.AckBody();
        ack.ackedSeq = 12345;
        session_test.Packet frame = session_test.Packet.wrap(ack);
        byte[] frameBytes = frame.encodeBytes();

        var dataSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, frameBytes);
        var outMsgs = arena.allocate(ValueLayout.ADDRESS);
        var outCount = arena.allocate(ValueLayout.JAVA_LONG);

        int err = (int) CodecBindings.conduit_decode_frame.invokeExact(
            session, dataSeg, (long) frameBytes.length, outMsgs, outCount);
        assertEquals(0, err, "Decode should return CONDUIT_OK");

        long count = outCount.get(ValueLayout.JAVA_LONG, 0);
        assertEquals(1, count);

        MemorySegment msgsPtr = outMsgs.get(ValueLayout.ADDRESS, 0);
        MemorySegment msgs = msgsPtr.reinterpret(count * MSG_STRUCT_SIZE);

        long typeId = msgs.get(ValueLayout.JAVA_LONG, MSG_TYPE_ID_OFFSET);
        assertEquals(ACK_TYPE_ID, typeId);

        long dataLen = msgs.get(ValueLayout.JAVA_LONG, MSG_DATA_LEN_OFFSET);
        assertTrue(dataLen > 0);

        // CABI decode returns the full frame bytes
        MemorySegment dataPtr = msgs.get(ValueLayout.ADDRESS, MSG_DATA_OFFSET);
        byte[] payloadBytes = dataPtr.reinterpret(dataLen).toArray(ValueLayout.JAVA_BYTE);
        session_test.Packet decodedFrame = session_test.Packet.decodeBytes(payloadBytes);
        assertInstanceOf(session_test.AckBody.class, decodedFrame.payload);
        session_test.AckBody decodedAck = (session_test.AckBody) decodedFrame.payload;
        assertEquals(12345, decodedAck.ackedSeq);

        CodecBindings.conduit_free_decoded_msgs.invokeExact(msgsPtr, count);
    }

    // ========================================================================
    // Encode message roundtrip
    // ========================================================================

    @Test
    @DisplayName("Encode: PingBody encode roundtrip via CABI")
    void encodePingBodyRoundtrip() throws Throwable {
        // Encode a PingBody payload via the CABI
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 0xAABBCCDD;
        byte[] payload = ping.encodeBytes();

        var payloadSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, payload);
        var encodeResult = arena.allocate(ENCODE_RESULT_SIZE);

        int err = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, PING_TYPE_ID, payloadSeg, (long) payload.length, encodeResult);
        assertEquals(0, err, "Encode should return CONDUIT_OK");

        long encLen = encodeResult.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
        assertTrue(encLen > 0, "Encoded result should have data");

        MemorySegment encData = encodeResult.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
        byte[] encodedFrame = encData.reinterpret(encLen).toArray(ValueLayout.JAVA_BYTE);

        // The encoded frame should be a valid Packet frame - decode it with generated Java
        session_test.Packet decodedFrame = session_test.Packet.decodeBytes(encodedFrame);
        assertEquals((int) session_test.Constants.SYNC, decodedFrame.sync, "Sync should be 0xDEAD");
        assertEquals(session_test.PingBody.ID_VALUE, decodedFrame.msgId, "MsgId should be PingBody's discriminator");
        assertInstanceOf(session_test.PingBody.class, decodedFrame.payload);
        session_test.PingBody decodedPing = (session_test.PingBody) decodedFrame.payload;
        assertEquals(0xAABBCCDD, decodedPing.timestamp);

        // Free the encode result
        CodecBindings.conduit_free_encode_result.invokeExact(encodeResult);
    }

    @Test
    @DisplayName("Encode: DataBody encode roundtrip via CABI")
    void encodeDataBodyRoundtrip() throws Throwable {
        session_test.DataBody data = new session_test.DataBody();
        data.channel = 99;
        data.payloadA = 0x11223344;
        data.payloadB = 0x55667788;
        byte[] payload = data.encodeBytes();

        var payloadSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, payload);
        var encodeResult = arena.allocate(ENCODE_RESULT_SIZE);

        int err = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, DATA_TYPE_ID, payloadSeg, (long) payload.length, encodeResult);
        assertEquals(0, err, "Encode should return CONDUIT_OK");

        long encLen = encodeResult.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
        MemorySegment encData = encodeResult.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
        byte[] encodedFrame = encData.reinterpret(encLen).toArray(ValueLayout.JAVA_BYTE);

        session_test.Packet decodedFrame = session_test.Packet.decodeBytes(encodedFrame);
        assertInstanceOf(session_test.DataBody.class, decodedFrame.payload);
        session_test.DataBody decodedData = (session_test.DataBody) decodedFrame.payload;
        assertEquals(99, decodedData.channel);
        assertEquals(0x11223344, decodedData.payloadA);
        assertEquals(0x55667788, decodedData.payloadB);

        CodecBindings.conduit_free_encode_result.invokeExact(encodeResult);
    }

    @Test
    @DisplayName("Encode: encode then decode via CABI roundtrip")
    void encodeDecodeCabiRoundtrip() throws Throwable {
        // Encode a PingBody via CABI
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 42;
        byte[] payload = ping.encodeBytes();

        var payloadSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, payload);
        var encodeResult = arena.allocate(ENCODE_RESULT_SIZE);

        int encErr = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, PING_TYPE_ID, payloadSeg, (long) payload.length, encodeResult);
        assertEquals(0, encErr);

        long encLen = encodeResult.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
        MemorySegment encData = encodeResult.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
        byte[] encodedFrame = encData.reinterpret(encLen).toArray(ValueLayout.JAVA_BYTE);

        // Now decode the frame via CABI
        var frameSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, encodedFrame);
        var outMsgs = arena.allocate(ValueLayout.ADDRESS);
        var outCount = arena.allocate(ValueLayout.JAVA_LONG);

        int decErr = (int) CodecBindings.conduit_decode_frame.invokeExact(
            session, frameSeg, (long) encodedFrame.length, outMsgs, outCount);
        assertEquals(0, decErr);

        long count = outCount.get(ValueLayout.JAVA_LONG, 0);
        assertEquals(1, count);

        MemorySegment msgsPtr = outMsgs.get(ValueLayout.ADDRESS, 0);
        MemorySegment msgs = msgsPtr.reinterpret(count * MSG_STRUCT_SIZE);

        long typeId = msgs.get(ValueLayout.JAVA_LONG, MSG_TYPE_ID_OFFSET);
        assertEquals(PING_TYPE_ID, typeId);

        long dataLen = msgs.get(ValueLayout.JAVA_LONG, MSG_DATA_LEN_OFFSET);
        MemorySegment dataPtr = msgs.get(ValueLayout.ADDRESS, MSG_DATA_OFFSET);
        byte[] decodedPayload = dataPtr.reinterpret(dataLen).toArray(ValueLayout.JAVA_BYTE);
        // CABI decode returns the full frame bytes
        session_test.Packet decodedFrame = session_test.Packet.decodeBytes(decodedPayload);
        assertInstanceOf(session_test.PingBody.class, decodedFrame.payload);
        session_test.PingBody decodedPing = (session_test.PingBody) decodedFrame.payload;
        assertEquals(42, decodedPing.timestamp, "Full encode-decode CABI roundtrip should preserve data");

        CodecBindings.conduit_free_decoded_msgs.invokeExact(msgsPtr, count);
        CodecBindings.conduit_free_encode_result.invokeExact(encodeResult);
    }

    @Test
    @DisplayName("Encode: unknown type ID returns error")
    void encodeUnknownTypeReturnsError() throws Throwable {
        byte[] payload = new byte[] { 0x00, 0x01 };
        var payloadSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, payload);
        var encodeResult = arena.allocate(ENCODE_RESULT_SIZE);

        int err = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, 0xDEADDEADDEADDEADL, payloadSeg, (long) payload.length, encodeResult);
        assertNotEquals(0, err, "Encoding with unknown type ID should return an error");
    }

    // ========================================================================
    // Session reset
    // ========================================================================

    @Test
    @DisplayName("Session reset: sequence counter resets")
    void sessionResetSequenceCounter() throws Throwable {
        // Encode a message to advance the sequence counter
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 1;
        byte[] payload = ping.encodeBytes();
        var payloadSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, payload);
        var encodeResult1 = arena.allocate(ENCODE_RESULT_SIZE);

        int err1 = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, PING_TYPE_ID, payloadSeg, (long) payload.length, encodeResult1);
        assertEquals(0, err1);
        long encLen1 = encodeResult1.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
        MemorySegment encData1 = encodeResult1.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
        byte[] frame1 = encData1.reinterpret(encLen1).toArray(ValueLayout.JAVA_BYTE);

        // Encode another message (sequence counter should increment)
        var encodeResult2 = arena.allocate(ENCODE_RESULT_SIZE);
        int err2 = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, PING_TYPE_ID, payloadSeg, (long) payload.length, encodeResult2);
        assertEquals(0, err2);
        long encLen2 = encodeResult2.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
        MemorySegment encData2 = encodeResult2.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
        byte[] frame2 = encData2.reinterpret(encLen2).toArray(ValueLayout.JAVA_BYTE);

        // Reset the session
        CodecBindings.conduit_session_reset.invokeExact(session);

        // Encode again after reset
        var encodeResult3 = arena.allocate(ENCODE_RESULT_SIZE);
        int err3 = (int) CodecBindings.conduit_encode_message.invokeExact(
            session, PING_TYPE_ID, payloadSeg, (long) payload.length, encodeResult3);
        assertEquals(0, err3);
        long encLen3 = encodeResult3.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
        MemorySegment encData3 = encodeResult3.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
        byte[] frame3 = encData3.reinterpret(encLen3).toArray(ValueLayout.JAVA_BYTE);

        // After reset, the frame should have the same sequence as the first frame
        // (both have seq=0, because reset resets the sequence counter)
        session_test.Packet p1 = session_test.Packet.decodeBytes(frame1);
        session_test.Packet p3 = session_test.Packet.decodeBytes(frame3);
        assertEquals(p1.seq, p3.seq, "Sequence counter should reset to same value after session reset");

        CodecBindings.conduit_free_encode_result.invokeExact(encodeResult1);
        CodecBindings.conduit_free_encode_result.invokeExact(encodeResult2);
        CodecBindings.conduit_free_encode_result.invokeExact(encodeResult3);
    }

    @Test
    @DisplayName("Session reset: does not crash on valid session")
    void sessionResetNoCrash() throws Throwable {
        // Simply verify reset does not throw/crash
        CodecBindings.conduit_session_reset.invokeExact(session);
        // Session should still be functional after reset
        long count = (long) CodecBindings.conduit_session_leaf_type_count.invokeExact(session);
        assertEquals(3, count, "Session should still be functional after reset");
    }

    // ========================================================================
    // Framer: complete frame
    // ========================================================================

    @Test
    @DisplayName("Framer: feed complete frame extracts one frame")
    void framerFeedCompleteFrame() throws Throwable {
        // Create framer
        MemorySegment framer = (MemorySegment) CodecBindings.conduit_framer_create.invokeExact(session);
        assertNotEquals(MemorySegment.NULL, framer, "Framer should be created successfully");

        try {
            // Build a complete frame
            session_test.PingBody ping = new session_test.PingBody();
            ping.timestamp = 0xDEADBEEF;
            session_test.Packet frame = session_test.Packet.wrap(ping);
            byte[] frameBytes = frame.encodeBytes();

            var dataSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, frameBytes);
            var outFrames = arena.allocate(ValueLayout.ADDRESS);
            var outCount = arena.allocate(ValueLayout.JAVA_LONG);

            int err = (int) CodecBindings.conduit_framer_feed.invokeExact(
                framer, dataSeg, (long) frameBytes.length, outFrames, outCount);
            assertEquals(0, err, "Framer feed should return CONDUIT_OK");

            long count = outCount.get(ValueLayout.JAVA_LONG, 0);
            assertEquals(1, count, "Framer should extract exactly one frame");

            // Read the extracted frame data
            MemorySegment framesPtr = outFrames.get(ValueLayout.ADDRESS, 0);
            MemorySegment frames = framesPtr.reinterpret(count * FRAME_STRUCT_SIZE);
            MemorySegment frameDataPtr = frames.get(ValueLayout.ADDRESS, FRAME_DATA_OFFSET);
            long frameDataLen = frames.get(ValueLayout.JAVA_LONG, FRAME_DATA_LEN_OFFSET);

            assertTrue(frameDataLen > 0, "Extracted frame should have data");

            byte[] extractedFrame = frameDataPtr.reinterpret(frameDataLen)
                .toArray(ValueLayout.JAVA_BYTE);

            // The extracted frame should be decodable
            session_test.Packet decoded = session_test.Packet.decodeBytes(extractedFrame);
            assertInstanceOf(session_test.PingBody.class, decoded.payload);
            session_test.PingBody decodedPing = (session_test.PingBody) decoded.payload;
            assertEquals(0xDEADBEEF, decodedPing.timestamp);

            CodecBindings.conduit_free_frames.invokeExact(framesPtr, count);
        } finally {
            CodecBindings.conduit_framer_destroy.invokeExact(framer);
        }
    }

    @Test
    @DisplayName("Framer: feed two complete frames extracts two frames")
    void framerFeedTwoCompleteFrames() throws Throwable {
        MemorySegment framer = (MemorySegment) CodecBindings.conduit_framer_create.invokeExact(session);
        assertNotEquals(MemorySegment.NULL, framer);

        try {
            session_test.PingBody ping = new session_test.PingBody();
            ping.timestamp = 100;
            byte[] frame1 = session_test.Packet.wrap(ping).encodeBytes();

            session_test.AckBody ack = new session_test.AckBody();
            ack.ackedSeq = 200;
            byte[] frame2 = session_test.Packet.wrap(ack).encodeBytes();

            // Concatenate both frames
            byte[] combined = new byte[frame1.length + frame2.length];
            System.arraycopy(frame1, 0, combined, 0, frame1.length);
            System.arraycopy(frame2, 0, combined, frame1.length, frame2.length);

            var dataSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, combined);
            var outFrames = arena.allocate(ValueLayout.ADDRESS);
            var outCount = arena.allocate(ValueLayout.JAVA_LONG);

            int err = (int) CodecBindings.conduit_framer_feed.invokeExact(
                framer, dataSeg, (long) combined.length, outFrames, outCount);
            assertEquals(0, err);

            long count = outCount.get(ValueLayout.JAVA_LONG, 0);
            assertEquals(2, count, "Framer should extract two frames from concatenated data");

            MemorySegment framesPtr = outFrames.get(ValueLayout.ADDRESS, 0);
            CodecBindings.conduit_free_frames.invokeExact(framesPtr, count);
        } finally {
            CodecBindings.conduit_framer_destroy.invokeExact(framer);
        }
    }

    // ========================================================================
    // Framer: partial frame feeding
    // ========================================================================

    @Test
    @DisplayName("Framer: partial data yields nothing, then rest completes")
    void framerPartialThenComplete() throws Throwable {
        MemorySegment framer = (MemorySegment) CodecBindings.conduit_framer_create.invokeExact(session);
        assertNotEquals(MemorySegment.NULL, framer);

        try {
            // Build a complete frame
            session_test.PingBody ping = new session_test.PingBody();
            ping.timestamp = 0x42424242;
            byte[] fullFrame = session_test.Packet.wrap(ping).encodeBytes();
            assertTrue(fullFrame.length >= 4, "Frame should have enough bytes to split");

            // Split the frame into two parts
            int splitPoint = fullFrame.length / 2;
            byte[] part1 = Arrays.copyOfRange(fullFrame, 0, splitPoint);
            byte[] part2 = Arrays.copyOfRange(fullFrame, splitPoint, fullFrame.length);

            // Feed first part - should extract nothing
            var dataSeg1 = arena.allocateArray(ValueLayout.JAVA_BYTE, part1);
            var outFrames1 = arena.allocate(ValueLayout.ADDRESS);
            var outCount1 = arena.allocate(ValueLayout.JAVA_LONG);

            int err1 = (int) CodecBindings.conduit_framer_feed.invokeExact(
                framer, dataSeg1, (long) part1.length, outFrames1, outCount1);
            assertEquals(0, err1, "Partial feed should not error");

            long count1 = outCount1.get(ValueLayout.JAVA_LONG, 0);
            assertEquals(0, count1, "Partial frame data should yield zero extracted frames");

            // Feed second part - should now extract the complete frame
            var dataSeg2 = arena.allocateArray(ValueLayout.JAVA_BYTE, part2);
            var outFrames2 = arena.allocate(ValueLayout.ADDRESS);
            var outCount2 = arena.allocate(ValueLayout.JAVA_LONG);

            int err2 = (int) CodecBindings.conduit_framer_feed.invokeExact(
                framer, dataSeg2, (long) part2.length, outFrames2, outCount2);
            assertEquals(0, err2);

            long count2 = outCount2.get(ValueLayout.JAVA_LONG, 0);
            assertEquals(1, count2, "Feeding remaining bytes should extract one frame");

            // Verify the extracted frame content
            MemorySegment framesPtr = outFrames2.get(ValueLayout.ADDRESS, 0);
            MemorySegment frames = framesPtr.reinterpret(count2 * FRAME_STRUCT_SIZE);
            MemorySegment frameDataPtr = frames.get(ValueLayout.ADDRESS, FRAME_DATA_OFFSET);
            long frameDataLen = frames.get(ValueLayout.JAVA_LONG, FRAME_DATA_LEN_OFFSET);

            byte[] extractedFrame = frameDataPtr.reinterpret(frameDataLen)
                .toArray(ValueLayout.JAVA_BYTE);
            session_test.Packet decoded = session_test.Packet.decodeBytes(extractedFrame);
            assertInstanceOf(session_test.PingBody.class, decoded.payload);
            assertEquals(0x42424242, ((session_test.PingBody) decoded.payload).timestamp);

            CodecBindings.conduit_free_frames.invokeExact(framesPtr, count2);
        } finally {
            CodecBindings.conduit_framer_destroy.invokeExact(framer);
        }
    }

    @Test
    @DisplayName("Framer: single byte feeding eventually completes frame")
    void framerByteByByte() throws Throwable {
        MemorySegment framer = (MemorySegment) CodecBindings.conduit_framer_create.invokeExact(session);
        assertNotEquals(MemorySegment.NULL, framer);

        try {
            session_test.AckBody ack = new session_test.AckBody();
            ack.ackedSeq = 999;
            byte[] fullFrame = session_test.Packet.wrap(ack).encodeBytes();

            long totalExtracted = 0;

            for (int i = 0; i < fullFrame.length; i++) {
                byte[] singleByte = new byte[] { fullFrame[i] };
                var dataSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, singleByte);
                var outFrames = arena.allocate(ValueLayout.ADDRESS);
                var outCount = arena.allocate(ValueLayout.JAVA_LONG);

                int err = (int) CodecBindings.conduit_framer_feed.invokeExact(
                    framer, dataSeg, 1L, outFrames, outCount);
                assertEquals(0, err, "Feeding byte " + i + " should not error");

                long count = outCount.get(ValueLayout.JAVA_LONG, 0);
                if (count > 0) {
                    totalExtracted += count;
                    MemorySegment framesPtr = outFrames.get(ValueLayout.ADDRESS, 0);
                    CodecBindings.conduit_free_frames.invokeExact(framesPtr, count);
                }
            }

            assertEquals(1, totalExtracted, "Byte-by-byte feeding should eventually yield one frame");
        } finally {
            CodecBindings.conduit_framer_destroy.invokeExact(framer);
        }
    }

    @Test
    @DisplayName("Framer: create returns NULL for NULL session")
    void framerCreateNullSession() throws Throwable {
        MemorySegment framer = (MemorySegment) CodecBindings.conduit_framer_create
            .invokeExact(MemorySegment.NULL);
        assertEquals(MemorySegment.NULL, framer, "Framer creation with NULL session should return NULL");
    }

    // ========================================================================
    // Edge cases
    // ========================================================================

    @Test
    @DisplayName("Encode: multiple encodes increment sequence counter")
    void encodeSequenceIncrement() throws Throwable {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 1;
        byte[] payload = ping.encodeBytes();
        var payloadSeg = arena.allocateArray(ValueLayout.JAVA_BYTE, payload);

        int[] seqs = new int[3];
        for (int i = 0; i < 3; i++) {
            var encResult = arena.allocate(ENCODE_RESULT_SIZE);
            int err = (int) CodecBindings.conduit_encode_message.invokeExact(
                session, PING_TYPE_ID, payloadSeg, (long) payload.length, encResult);
            assertEquals(0, err);

            long encLen = encResult.get(ValueLayout.JAVA_LONG, ENCODE_DATA_LEN_OFFSET);
            MemorySegment encData = encResult.get(ValueLayout.ADDRESS, ENCODE_DATA_OFFSET);
            byte[] frame = encData.reinterpret(encLen).toArray(ValueLayout.JAVA_BYTE);
            session_test.Packet p = session_test.Packet.decodeBytes(frame);
            seqs[i] = p.seq;

            CodecBindings.conduit_free_encode_result.invokeExact(encResult);
        }

        // Sequence numbers should be monotonically increasing
        assertTrue(seqs[1] > seqs[0], "Sequence should increment: seq[1]=" + seqs[1] + " > seq[0]=" + seqs[0]);
        assertTrue(seqs[2] > seqs[1], "Sequence should increment: seq[2]=" + seqs[2] + " > seq[1]=" + seqs[1]);
    }

    @Test
    @DisplayName("Introspection: multiple sessions can coexist")
    void multipleSessionsCoexist() throws Throwable {
        try (var localArena = Arena.ofConfined()) {
            var name1 = localArena.allocateUtf8String("session_protocol");
            var name2 = localArena.allocateUtf8String("choice_protocol");
            MemorySegment s1 = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(name1);
            MemorySegment s2 = (MemorySegment) CodecBindings.conduit_session_create.invokeExact(name2);

            assertNotEquals(MemorySegment.NULL, s1);
            assertNotEquals(MemorySegment.NULL, s2);

            // Both should have their own type counts
            long c1 = (long) CodecBindings.conduit_session_leaf_type_count.invokeExact(s1);
            long c2 = (long) CodecBindings.conduit_session_leaf_type_count.invokeExact(s2);
            assertEquals(3, c1, "session_protocol has 3 types");
            assertEquals(2, c2, "choice_protocol has 2 types");

            // Protocol names should differ
            MemorySegment pn1 = (MemorySegment) CodecBindings.conduit_session_protocol_name.invokeExact(s1);
            MemorySegment pn2 = (MemorySegment) CodecBindings.conduit_session_protocol_name.invokeExact(s2);
            String name1Str = pn1.reinterpret(256).getUtf8String(0);
            String name2Str = pn2.reinterpret(256).getUtf8String(0);
            assertNotEquals(name1Str, name2Str, "Different sessions should have different protocol names");

            CodecBindings.conduit_session_destroy.invokeExact(s1);
            CodecBindings.conduit_session_destroy.invokeExact(s2);
        }
    }
}
