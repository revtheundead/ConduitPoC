// SPDX-License-Identifier: MIT
// JNI bridge for the Conduit Transceiver C ABI.
// Implements the native methods declared in io.conduit.JniNativeBinding.

#include <jni.h>
#include <conduit/cabi/conduit_cabi.h>
#include <cstring>
#include <mutex>
#include <unordered_map>

// ============================================================================
// JVM & class caching
// ============================================================================

static JavaVM* g_jvm = nullptr;
static jclass g_binding_class = nullptr;
static jmethodID g_dispatch_msg = nullptr;
static jmethodID g_dispatch_state = nullptr;
static jmethodID g_dispatch_error = nullptr;

/**
 * Get the JNIEnv* for the current thread, attaching as a daemon thread
 * if necessary. Daemon-attached threads are automatically cleaned up by
 * the JVM when it shuts down, avoiding the need for explicit detach.
 *
 * @param[out] was_attached  Set to true if the thread was already attached
 * @return JNIEnv* or nullptr on failure
 */
static JNIEnv* get_env() {
    JNIEnv* env = nullptr;
    if (g_jvm) {
        int status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            // Attach as daemon thread so the JVM doesn't wait for it on exit
            // and it gets automatically cleaned up
            g_jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);
        }
    }
    return env;
}

// ============================================================================
// Callback relay structures
// ============================================================================

struct JniMsgCallbackData {
    int callback_key;
};

struct JniStateCallbackData {
    int callback_key;
};

struct JniErrorCallbackData {
    int callback_key;
};

// Global callback data storage (prevent premature free of data passed to C)
static std::mutex g_cb_mutex;
static std::unordered_map<int, JniMsgCallbackData*> g_msg_cbs;
static std::unordered_map<int, JniStateCallbackData*> g_state_cbs;
static std::unordered_map<int, JniErrorCallbackData*> g_error_cbs;

// ============================================================================
// C callback trampolines (called from native code, dispatch to Java)
// ============================================================================

static void msg_callback_trampoline(
    conduit_peer_id peer, uint64_t type_id, const char* type_name,
    const uint8_t* data, size_t len, void* user_data) {

    auto* cbd = static_cast<JniMsgCallbackData*>(user_data);
    JNIEnv* env = get_env();
    if (!env || !g_binding_class || !g_dispatch_msg) return;

    jstring jtypeName = env->NewStringUTF(type_name ? type_name : "");
    jbyteArray jdata = env->NewByteArray(static_cast<jsize>(len));
    if (data && len > 0) {
        env->SetByteArrayRegion(jdata, 0, static_cast<jsize>(len),
                                reinterpret_cast<const jbyte*>(data));
    }

    env->CallStaticVoidMethod(g_binding_class, g_dispatch_msg,
        cbd->callback_key,
        static_cast<jint>(peer),
        static_cast<jlong>(type_id),
        jtypeName, jdata);

    env->DeleteLocalRef(jtypeName);
    env->DeleteLocalRef(jdata);
}

static void state_callback_trampoline(
    conduit_peer_id peer, int32_t new_state, void* user_data) {

    auto* cbd = static_cast<JniStateCallbackData*>(user_data);
    JNIEnv* env = get_env();
    if (!env || !g_binding_class || !g_dispatch_state) return;

    env->CallStaticVoidMethod(g_binding_class, g_dispatch_state,
        cbd->callback_key,
        static_cast<jint>(peer),
        static_cast<jint>(new_state));
}

static void error_callback_trampoline(
    conduit_peer_id peer, const char* peer_name,
    int32_t error_code, const char* error_message, void* user_data) {

    auto* cbd = static_cast<JniErrorCallbackData*>(user_data);
    JNIEnv* env = get_env();
    if (!env || !g_binding_class || !g_dispatch_error) return;

    jstring jpeerName = env->NewStringUTF(peer_name ? peer_name : "");
    jstring jerrorMsg = env->NewStringUTF(error_message ? error_message : "");

    env->CallStaticVoidMethod(g_binding_class, g_dispatch_error,
        cbd->callback_key,
        static_cast<jint>(peer),
        jpeerName,
        static_cast<jint>(error_code),
        jerrorMsg);

    env->DeleteLocalRef(jpeerName);
    env->DeleteLocalRef(jerrorMsg);
}

// ============================================================================
// JNI_OnLoad — cache JVM reference and method IDs
// ============================================================================

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_jvm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass cls = env->FindClass("io/conduit/JniNativeBinding");
    if (!cls) return JNI_ERR;
    g_binding_class = static_cast<jclass>(env->NewGlobalRef(cls));
    env->DeleteLocalRef(cls);

    g_dispatch_msg = env->GetStaticMethodID(g_binding_class, "dispatchMessage",
        "(IIJLjava/lang/String;[B)V");
    g_dispatch_state = env->GetStaticMethodID(g_binding_class, "dispatchStateChange",
        "(III)V");
    g_dispatch_error = env->GetStaticMethodID(g_binding_class, "dispatchError",
        "(IILjava/lang/String;ILjava/lang/String;)V");

    if (!g_dispatch_msg || !g_dispatch_state || !g_dispatch_error) return JNI_ERR;

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    // Cleanup global callback data
    std::lock_guard<std::mutex> lock(g_cb_mutex);
    for (auto& [k, v] : g_msg_cbs) delete v;
    for (auto& [k, v] : g_state_cbs) delete v;
    for (auto& [k, v] : g_error_cbs) delete v;
    g_msg_cbs.clear();
    g_state_cbs.clear();
    g_error_cbs.clear();

    JNIEnv* env = get_env();
    if (env && g_binding_class) {
        env->DeleteGlobalRef(g_binding_class);
        g_binding_class = nullptr;
    }
    g_jvm = nullptr;
}

// ============================================================================
// Native method implementations
// ============================================================================

extern "C" {

// Lifecycle
JNIEXPORT jlong JNICALL Java_io_conduit_JniNativeBinding_nCreate(JNIEnv*, jclass) {
    auto* xcvr = conduit_create();
    return reinterpret_cast<jlong>(xcvr);
}

JNIEXPORT void JNICALL Java_io_conduit_JniNativeBinding_nDestroy(JNIEnv*, jclass, jlong handle) {
    if (handle == 0) return;
    conduit_destroy(reinterpret_cast<conduit_transceiver_t*>(handle));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nStart(JNIEnv*, jclass, jlong handle) {
    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    return conduit_start(reinterpret_cast<conduit_transceiver_t*>(handle));
}

JNIEXPORT void JNICALL Java_io_conduit_JniNativeBinding_nStop(JNIEnv*, jclass, jlong handle) {
    if (handle == 0) return;
    conduit_stop(reinterpret_cast<conduit_transceiver_t*>(handle));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nIsRunning(JNIEnv*, jclass, jlong handle) {
    if (handle == 0) return 0;
    return conduit_is_running(reinterpret_cast<conduit_transceiver_t*>(handle));
}

// Peer management
JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nAddPeer(
    JNIEnv* env, jclass, jlong handle,
    jstring jname, jstring jsessionName,
    jint transportType, jstring jaddress, jlong baudRate,
    jlong recvBufferSize, jlong connectTimeoutMs,
    jint reconnectEnabled,
    jlong reconnectInitialDelayMs, jlong reconnectMaxDelayMs,
    jdouble reconnectBackoffMul, jlong reconnectMaxAttempts,
    jstring jbindAddress, jint bindPort, jint remotePort,
    jlong maxDatagramSize, jlong maxPeers, jlong peerTimeoutS,
    jlong maxClients,
    jint dataBits, jint parity, jint stopBits, jint flowControl) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    const char* name        = env->GetStringUTFChars(jname, nullptr);
    const char* sessionName = env->GetStringUTFChars(jsessionName, nullptr);
    const char* address     = env->GetStringUTFChars(jaddress, nullptr);
    const char* bindAddress = jbindAddress ? env->GetStringUTFChars(jbindAddress, nullptr) : nullptr;

    conduit_transport_config_t cfg = {};
    cfg.type                       = static_cast<conduit_transport_type_t>(transportType);
    cfg.address                    = address;
    cfg.baud_rate                  = static_cast<uint32_t>(baudRate);
    cfg.recv_buffer_size           = static_cast<size_t>(recvBufferSize);
    cfg.connect_timeout_ms         = static_cast<uint32_t>(connectTimeoutMs);
    cfg.reconnect_enabled          = static_cast<int>(reconnectEnabled);
    cfg.reconnect_initial_delay_ms = static_cast<uint32_t>(reconnectInitialDelayMs);
    cfg.reconnect_max_delay_ms     = static_cast<uint32_t>(reconnectMaxDelayMs);
    cfg.reconnect_backoff_multiplier = static_cast<double>(reconnectBackoffMul);
    cfg.reconnect_max_attempts     = static_cast<uint32_t>(reconnectMaxAttempts);
    cfg.bind_address               = bindAddress;
    cfg.bind_port                  = static_cast<uint16_t>(bindPort);
    cfg.remote_port                = static_cast<uint16_t>(remotePort);
    cfg.max_datagram_size          = static_cast<size_t>(maxDatagramSize);
    cfg.max_peers                  = static_cast<size_t>(maxPeers);
    cfg.peer_timeout_s             = static_cast<uint32_t>(peerTimeoutS);
    cfg.max_clients                = static_cast<size_t>(maxClients);
    cfg.data_bits                  = static_cast<uint8_t>(dataBits);
    cfg.parity                     = static_cast<int>(parity);
    cfg.stop_bits                  = static_cast<int>(stopBits);
    cfg.flow_control               = static_cast<int>(flowControl);

    conduit_peer_id peer_id = 0;
    int err = conduit_add_peer(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        name, sessionName, &cfg, &peer_id);

    if (bindAddress) env->ReleaseStringUTFChars(jbindAddress, bindAddress);
    env->ReleaseStringUTFChars(jaddress, address);
    env->ReleaseStringUTFChars(jsessionName, sessionName);
    env->ReleaseStringUTFChars(jname, name);

    if (err != 0) return err;
    return static_cast<jint>(peer_id);
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSolePeer(JNIEnv*, jclass, jlong handle) {
    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    conduit_peer_id pid = 0;
    int err = conduit_sole_peer(
        reinterpret_cast<conduit_transceiver_t*>(handle), &pid);
    return err != 0 ? err : static_cast<jint>(pid);
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nPeerByName(
    JNIEnv* env, jclass, jlong handle, jstring jname) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    const char* name = env->GetStringUTFChars(jname, nullptr);
    conduit_peer_id pid = 0;
    int err = conduit_peer_by_name(
        reinterpret_cast<conduit_transceiver_t*>(handle), name, &pid);
    env->ReleaseStringUTFChars(jname, name);
    return err != 0 ? err : static_cast<jint>(pid);
}

JNIEXPORT jlong JNICALL Java_io_conduit_JniNativeBinding_nPeerCount(JNIEnv*, jclass, jlong handle) {
    if (handle == 0) return 0;
    return static_cast<jlong>(
        conduit_peer_count(reinterpret_cast<conduit_transceiver_t*>(handle)));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nPeerState(
    JNIEnv*, jclass, jlong handle, jint peerId) {

    if (handle == 0) return -1;
    return conduit_peer_state(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<conduit_peer_id>(peerId));
}

// Messaging
JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSend(
    JNIEnv* env, jclass, jlong handle,
    jint peerId, jlong typeId, jbyteArray jdata, jint len) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    jbyte* data = env->GetByteArrayElements(jdata, nullptr);
    int err = conduit_send(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<conduit_peer_id>(peerId),
        static_cast<uint64_t>(typeId),
        reinterpret_cast<const uint8_t*>(data),
        static_cast<size_t>(len));
    env->ReleaseByteArrayElements(jdata, data, JNI_ABORT);
    return err;
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSendBatch(
    JNIEnv* env, jclass, jlong handle,
    jint peerId, jlong typeId,
    jobjectArray jpayloads, jintArray /*jlengths*/, jint count) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    // Build C arrays of pointers and lengths
    auto** payloads = new const uint8_t*[count];
    auto* lens = new size_t[count];
    auto** jbuffers = new jbyte*[count];
    auto* jarrs = new jbyteArray[count]; // save refs for proper release

    for (int i = 0; i < count; i++) {
        jarrs[i] = static_cast<jbyteArray>(env->GetObjectArrayElement(jpayloads, i));
        jbuffers[i] = env->GetByteArrayElements(jarrs[i], nullptr);
        payloads[i] = reinterpret_cast<const uint8_t*>(jbuffers[i]);
        lens[i] = static_cast<size_t>(env->GetArrayLength(jarrs[i]));
    }

    int err = conduit_send_batch(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<conduit_peer_id>(peerId),
        static_cast<uint64_t>(typeId),
        payloads, lens, static_cast<size_t>(count));

    // Release all byte arrays using saved references
    for (int i = 0; i < count; i++) {
        env->ReleaseByteArrayElements(jarrs[i], jbuffers[i], JNI_ABORT);
        env->DeleteLocalRef(jarrs[i]);
    }
    delete[] payloads;
    delete[] lens;
    delete[] jbuffers;
    delete[] jarrs;

    return err;
}

// Handler registration
JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nOnMessage(
    JNIEnv*, jclass, jlong handle, jlong typeId, jint callbackKey) {

    if (handle == 0) return 0;

    auto* cbd = new JniMsgCallbackData{callbackKey};
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        g_msg_cbs[callbackKey] = cbd;
    }

    return static_cast<jint>(conduit_on_message(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<uint64_t>(typeId),
        msg_callback_trampoline,
        cbd));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nOnAnyMessage(
    JNIEnv*, jclass, jlong handle, jint callbackKey) {

    if (handle == 0) return 0;

    auto* cbd = new JniMsgCallbackData{callbackKey};
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        g_msg_cbs[callbackKey] = cbd;
    }

    return static_cast<jint>(conduit_on_any_message(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        msg_callback_trampoline,
        cbd));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nRemoveHandler(
    JNIEnv*, jclass, jlong handle, jint peerId, jlong typeId) {

    if (handle == 0) return 0;
    return conduit_remove_handler(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<conduit_peer_id>(peerId),
        static_cast<uint64_t>(typeId));
}

// State & error callbacks
JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nOnStateChange(
    JNIEnv*, jclass, jlong handle, jint callbackKey) {

    if (handle == 0) return 0;

    auto* cbd = new JniStateCallbackData{callbackKey};
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        g_state_cbs[callbackKey] = cbd;
    }

    return static_cast<jint>(conduit_on_state_change(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        state_callback_trampoline,
        cbd));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nRemoveStateChange(
    JNIEnv*, jclass, jlong handle, jint callbackId) {

    if (handle == 0) return 0;
    return conduit_remove_state_change(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<conduit_callback_id>(callbackId));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nOnError(
    JNIEnv*, jclass, jlong handle, jint callbackKey) {

    if (handle == 0) return 0;

    auto* cbd = new JniErrorCallbackData{callbackKey};
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        g_error_cbs[callbackKey] = cbd;
    }

    return static_cast<jint>(conduit_on_error(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        error_callback_trampoline,
        cbd));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nRemoveErrorCallback(
    JNIEnv*, jclass, jlong handle, jint callbackId) {

    if (handle == 0) return 0;
    return conduit_remove_error_callback(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<conduit_callback_id>(callbackId));
}

// Stats
JNIEXPORT jlongArray JNICALL Java_io_conduit_JniNativeBinding_nStats(
    JNIEnv* env, jclass, jlong handle) {

    jlongArray result = env->NewLongArray(8);
    if (handle == 0) return result;

    conduit_stats_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    conduit_stats(reinterpret_cast<conduit_transceiver_t*>(handle), &snap);

    jlong values[8] = {
        static_cast<jlong>(snap.messages_received),
        static_cast<jlong>(snap.messages_dispatched),
        static_cast<jlong>(snap.messages_dropped),
        static_cast<jlong>(snap.decode_errors),
        static_cast<jlong>(snap.handler_errors),
        static_cast<jlong>(snap.handler_timeouts),
        static_cast<jlong>(snap.bytes_received),
        static_cast<jlong>(snap.bytes_sent)
    };
    env->SetLongArrayRegion(result, 0, 8, values);
    return result;
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nStatsReset(
    JNIEnv*, jclass, jlong handle) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    return conduit_stats_reset(reinterpret_cast<conduit_transceiver_t*>(handle));
}

// Pre-start configuration
JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSetQueueConfig(
    JNIEnv*, jclass, jlong handle, jlong capacity, jint dropPolicy,
    jdouble backPressureThreshold) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    return conduit_set_queue_config(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<size_t>(capacity),
        static_cast<int>(dropPolicy),
        static_cast<double>(backPressureThreshold));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSetWorkerConfig(
    JNIEnv*, jclass, jlong handle, jlong threadCount, jlong handlerTimeoutMs) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    return conduit_set_worker_config(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<size_t>(threadCount),
        static_cast<uint64_t>(handlerTimeoutMs));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSetShutdownTimeout(
    JNIEnv*, jclass, jlong handle, jlong timeoutMs) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    return conduit_set_shutdown_timeout(
        reinterpret_cast<conduit_transceiver_t*>(handle),
        static_cast<uint64_t>(timeoutMs));
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nSetMessageLogConfig(
    JNIEnv* env, jclass, jlong handle,
    jint enabled, jint mode, jint output,
    jstring jdirectory, jstring jprefix, jstring jfilename,
    jstring jsentFilename, jstring jreceivedFilename,
    jint includeMessageContent) {

    if (handle == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    const char* directory        = jdirectory        ? env->GetStringUTFChars(jdirectory,        nullptr) : nullptr;
    const char* prefix           = jprefix           ? env->GetStringUTFChars(jprefix,           nullptr) : nullptr;
    const char* filename         = jfilename         ? env->GetStringUTFChars(jfilename,         nullptr) : nullptr;
    const char* sentFilename     = jsentFilename     ? env->GetStringUTFChars(jsentFilename,     nullptr) : nullptr;
    const char* receivedFilename = jreceivedFilename ? env->GetStringUTFChars(jreceivedFilename, nullptr) : nullptr;

    conduit_message_log_config_t cfg = {};
    cfg.enabled                 = static_cast<int>(enabled);
    cfg.mode                    = static_cast<int>(mode);
    cfg.output                  = static_cast<int>(output);
    cfg.directory               = directory;
    cfg.prefix                  = prefix;
    cfg.filename                = filename;
    cfg.sent_filename           = sentFilename;
    cfg.received_filename       = receivedFilename;
    cfg.include_message_content = static_cast<int>(includeMessageContent);

    int err = conduit_set_message_log_config(
        reinterpret_cast<conduit_transceiver_t*>(handle), &cfg);

    if (receivedFilename) env->ReleaseStringUTFChars(jreceivedFilename, receivedFilename);
    if (sentFilename)     env->ReleaseStringUTFChars(jsentFilename,     sentFilename);
    if (filename)         env->ReleaseStringUTFChars(jfilename,         filename);
    if (prefix)           env->ReleaseStringUTFChars(jprefix,           prefix);
    if (directory)        env->ReleaseStringUTFChars(jdirectory,        directory);

    return err;
}

// Passthrough session registration
JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nRegisterPassthroughSession(
    JNIEnv* env, jclass,
    jstring jname,
    jbyteArray jsyncPattern,
    jint minHeaderSize,
    jint lengthSkipBits,
    jint lengthFieldBits,
    jint lengthBigEndian,
    jlongArray jtypeIds,
    jobjectArray jtypeNames,
    jintArray jreceiveOnly) {

    const char* name = env->GetStringUTFChars(jname, nullptr);

    conduit_frame_config_t frame_config;
    memset(&frame_config, 0, sizeof(frame_config));
    frame_config.min_header_size = static_cast<size_t>(minHeaderSize);
    frame_config.length_skip_bits = static_cast<size_t>(lengthSkipBits);
    frame_config.length_field_bits = static_cast<size_t>(lengthFieldBits);
    frame_config.length_big_endian = lengthBigEndian;

    // Sync pattern
    jbyte* syncBytes = nullptr;
    jsize syncLen = 0;
    if (jsyncPattern != nullptr) {
        syncLen = env->GetArrayLength(jsyncPattern);
        syncBytes = env->GetByteArrayElements(jsyncPattern, nullptr);
        frame_config.sync_pattern = reinterpret_cast<const uint8_t*>(syncBytes);
        frame_config.sync_pattern_len = static_cast<size_t>(syncLen);
    }

    // Type metadata
    jsize typeCount = jtypeIds ? env->GetArrayLength(jtypeIds) : 0;
    jlong* typeIds = typeCount > 0 ? env->GetLongArrayElements(jtypeIds, nullptr) : nullptr;
    jint* receiveOnly = (jreceiveOnly && typeCount > 0) ? env->GetIntArrayElements(jreceiveOnly, nullptr) : nullptr;

    auto* type_ids_c = new uint64_t[typeCount];
    auto** type_names_c = new const char*[typeCount];
    auto* recv_only_c = new int[typeCount];
    auto* jstrings = new jstring[typeCount]; // for release

    for (jsize i = 0; i < typeCount; i++) {
        type_ids_c[i] = static_cast<uint64_t>(typeIds[i]);
        jstrings[i] = static_cast<jstring>(env->GetObjectArrayElement(jtypeNames, i));
        type_names_c[i] = env->GetStringUTFChars(jstrings[i], nullptr);
        recv_only_c[i] = receiveOnly ? receiveOnly[i] : 0;
    }

    int err = conduit_register_passthrough_session(
        name, &frame_config,
        type_ids_c, type_names_c, recv_only_c,
        static_cast<size_t>(typeCount));

    // Release all JNI resources
    for (jsize i = 0; i < typeCount; i++) {
        env->ReleaseStringUTFChars(jstrings[i], type_names_c[i]);
        env->DeleteLocalRef(jstrings[i]);
    }
    delete[] type_ids_c;
    delete[] type_names_c;
    delete[] recv_only_c;
    delete[] jstrings;

    if (typeIds) env->ReleaseLongArrayElements(jtypeIds, typeIds, JNI_ABORT);
    if (receiveOnly) env->ReleaseIntArrayElements(jreceiveOnly, receiveOnly, JNI_ABORT);
    if (syncBytes) env->ReleaseByteArrayElements(jsyncPattern, syncBytes, JNI_ABORT);
    env->ReleaseStringUTFChars(jname, name);

    return err;
}

// Version
JNIEXPORT jstring JNICALL Java_io_conduit_JniNativeBinding_nVersion(JNIEnv* env, jclass) {
    const char* ver = conduit_version();
    return env->NewStringUTF(ver ? ver : "");
}

// ============================================================================
// Logger configuration
// ============================================================================

JNIEXPORT void JNICALL Java_io_conduit_JniNativeBinding_nSetLogLevel(
        JNIEnv*, jclass, jint level) {
    conduit_set_log_level(level);
}

JNIEXPORT jint JNICALL Java_io_conduit_JniNativeBinding_nGetLogLevel(
        JNIEnv*, jclass) {
    return conduit_get_log_level();
}

JNIEXPORT void JNICALL Java_io_conduit_JniNativeBinding_nLogAddConsoleSink(
        JNIEnv*, jclass, jint use_stderr, jint colorize) {
    conduit_log_add_console_sink(use_stderr, colorize);
}

JNIEXPORT void JNICALL Java_io_conduit_JniNativeBinding_nLogAddFileSink(
        JNIEnv* env, jclass, jstring path, jint append) {
    const char* p = env->GetStringUTFChars(path, nullptr);
    if (!p) return;
    conduit_log_add_file_sink(p, append);
    env->ReleaseStringUTFChars(path, p);
}

JNIEXPORT void JNICALL Java_io_conduit_JniNativeBinding_nLogClearSinks(
        JNIEnv*, jclass) {
    conduit_log_clear_sinks();
}

} // extern "C"
