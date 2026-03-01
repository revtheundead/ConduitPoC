// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * Abstraction over the native Conduit Codec C ABI.
 * <p>
 * Two implementations exist:
 * <ul>
 *   <li>{@code PanamaCodecBinding} — uses the Panama FFI (JDK 21+)</li>
 *   <li>{@code JniCodecBinding} — uses JNI (JDK 11+)</li>
 * </ul>
 */
public interface NativeCodecBinding extends AutoCloseable {

    // ================================================================
    // Session lifecycle
    // ================================================================

    /** Create a session by type name. Returns opaque handle, or 0 on failure. */
    long sessionCreate(String sessionType);

    /** Destroy a session. */
    void sessionDestroy(long session);

    /** Reset session state. */
    void sessionReset(long session);

    // ================================================================
    // Decode
    // ================================================================

    /**
     * Decoded message: type_id, type_name, data.
     */
    final class DecodedMessage {
        public final long typeId;
        public final String typeName;
        public final byte[] data;

        public DecodedMessage(long typeId, String typeName, byte[] data) {
            this.typeId = typeId;
            this.typeName = typeName;
            this.data = data;
        }
    }

    /**
     * Decode a frame.
     *
     * @param session  Session handle
     * @param data     Raw frame bytes
     * @return array of decoded messages (empty on failure)
     */
    DecodedMessage[] decodeFrame(long session, byte[] data);

    // ================================================================
    // Encode
    // ================================================================

    /**
     * Encode a message.
     *
     * @param session   Session handle
     * @param typeId    Message type ID
     * @param payload   Raw field payload bytes
     * @return encoded wire bytes, or null on error
     */
    byte[] encodeMessage(long session, long typeId, byte[] payload);

    /**
     * Encode a batch of messages.
     *
     * @param session   Session handle
     * @param typeId    Message type ID
     * @param payloads  Array of raw field payloads
     * @return encoded wire bytes, or null on error
     */
    byte[] encodeBatch(long session, long typeId, byte[][] payloads);

    // ================================================================
    // Introspection
    // ================================================================

    /** Get the type name for a given type ID. Returns empty string if unknown. */
    String sessionTypeName(long session, long typeId);

    /** Get the number of leaf types in the session. */
    long sessionLeafTypeCount(long session);

    /** Get the leaf type IDs. */
    long[] sessionLeafTypeIds(long session);

    /** Check if a type is receive-only. */
    boolean sessionIsReceiveOnly(long session, long typeId);

    /** Get the protocol name. */
    String sessionProtocolName(long session);

    // ================================================================
    // Human-readable formatting
    // ================================================================

    /**
     * Format a message as human-readable text.
     *
     * @return formatted string, or null on error
     */
    String formatMessage(long session, long typeId, byte[] payload);

    // ================================================================
    // Framing
    // ================================================================

    /** Create a framer for a session. Returns framer handle, or 0 on failure. */
    long framerCreate(long session);

    /** Destroy a framer. */
    void framerDestroy(long framer);

    /**
     * Feed data to the framer and extract complete frames.
     *
     * @return array of extracted frame byte arrays (empty if none)
     */
    byte[][] framerFeed(long framer, byte[] data);

    // ================================================================
    // Version
    // ================================================================

    /** Get the codec library version string. */
    String codecVersion();

    // ================================================================
    // Cleanup
    // ================================================================

    @Override
    void close();
}
