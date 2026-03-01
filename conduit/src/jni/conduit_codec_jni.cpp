// SPDX-License-Identifier: MIT
// JNI bridge for the Conduit Codec C ABI.
// Implements the native methods declared in io.conduit.JniCodecBinding.

#include <jni.h>
#include <conduit/cabi/conduit_codec_cabi.h>
#include <cstring>

// Cache the DecodedMessage class and constructor
static jclass g_decoded_msg_class = nullptr;
static jmethodID g_decoded_msg_ctor = nullptr;

static void ensure_decoded_msg_class(JNIEnv* env) {
    if (g_decoded_msg_class) return;
    jclass cls = env->FindClass("io/conduit/NativeCodecBinding$DecodedMessage");
    if (!cls) return;
    g_decoded_msg_class = static_cast<jclass>(env->NewGlobalRef(cls));
    env->DeleteLocalRef(cls);
    g_decoded_msg_ctor = env->GetMethodID(g_decoded_msg_class, "<init>",
        "(JLjava/lang/String;[B)V");
}

extern "C" {

// ============================================================================
// Session lifecycle
// ============================================================================

JNIEXPORT jlong JNICALL Java_io_conduit_JniCodecBinding_nSessionCreate(
    JNIEnv* env, jclass, jstring jsessionType) {

    const char* sessionType = env->GetStringUTFChars(jsessionType, nullptr);
    conduit_session_t* session = conduit_session_create(sessionType);
    env->ReleaseStringUTFChars(jsessionType, sessionType);
    return reinterpret_cast<jlong>(session);
}

JNIEXPORT void JNICALL Java_io_conduit_JniCodecBinding_nSessionDestroy(
    JNIEnv*, jclass, jlong session) {

    if (session == 0) return;
    conduit_session_destroy(reinterpret_cast<conduit_session_t*>(session));
}

JNIEXPORT void JNICALL Java_io_conduit_JniCodecBinding_nSessionReset(
    JNIEnv*, jclass, jlong session) {

    if (session == 0) return;
    conduit_session_reset(reinterpret_cast<conduit_session_t*>(session));
}

// ============================================================================
// Decode
// ============================================================================

JNIEXPORT jobjectArray JNICALL Java_io_conduit_JniCodecBinding_nDecodeFrame(
    JNIEnv* env, jclass, jlong session, jbyteArray jdata, jint len) {

    if (session == 0) return nullptr;
    ensure_decoded_msg_class(env);
    if (!g_decoded_msg_class) return nullptr;

    jbyte* data = env->GetByteArrayElements(jdata, nullptr);

    conduit_decoded_msg_t* msgs = nullptr;
    size_t count = 0;
    int err = conduit_decode_frame(
        reinterpret_cast<conduit_session_t*>(session),
        reinterpret_cast<const uint8_t*>(data),
        static_cast<size_t>(len),
        &msgs, &count);

    env->ReleaseByteArrayElements(jdata, data, JNI_ABORT);

    if (err != 0 || count == 0) return nullptr;

    jobjectArray result = env->NewObjectArray(
        static_cast<jsize>(count), g_decoded_msg_class, nullptr);

    for (size_t i = 0; i < count; i++) {
        jstring jtypeName = env->NewStringUTF(msgs[i].type_name ? msgs[i].type_name : "");
        jbyteArray jmsgData = env->NewByteArray(static_cast<jsize>(msgs[i].data_len));
        if (msgs[i].data && msgs[i].data_len > 0) {
            env->SetByteArrayRegion(jmsgData, 0, static_cast<jsize>(msgs[i].data_len),
                                    reinterpret_cast<const jbyte*>(msgs[i].data));
        }

        jobject decoded = env->NewObject(g_decoded_msg_class, g_decoded_msg_ctor,
            static_cast<jlong>(msgs[i].type_id), jtypeName, jmsgData);
        env->SetObjectArrayElement(result, static_cast<jsize>(i), decoded);

        env->DeleteLocalRef(jtypeName);
        env->DeleteLocalRef(jmsgData);
        env->DeleteLocalRef(decoded);
    }

    conduit_free_decoded_msgs(msgs, count);
    return result;
}

// ============================================================================
// Encode
// ============================================================================

JNIEXPORT jbyteArray JNICALL Java_io_conduit_JniCodecBinding_nEncodeMessage(
    JNIEnv* env, jclass, jlong session, jlong typeId,
    jbyteArray jpayload, jint payloadLen) {

    if (session == 0) return nullptr;

    jbyte* payload = env->GetByteArrayElements(jpayload, nullptr);

    conduit_encode_result_t result;
    memset(&result, 0, sizeof(result));
    int err = conduit_encode_message(
        reinterpret_cast<conduit_session_t*>(session),
        static_cast<uint64_t>(typeId),
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<size_t>(payloadLen),
        &result);

    env->ReleaseByteArrayElements(jpayload, payload, JNI_ABORT);

    if (err != 0) return nullptr;

    jbyteArray jresult = env->NewByteArray(static_cast<jsize>(result.data_len));
    env->SetByteArrayRegion(jresult, 0, static_cast<jsize>(result.data_len),
                            reinterpret_cast<const jbyte*>(result.data));

    conduit_free_encode_result(&result);
    return jresult;
}

JNIEXPORT jbyteArray JNICALL Java_io_conduit_JniCodecBinding_nEncodeBatch(
    JNIEnv* env, jclass, jlong session, jlong typeId,
    jobjectArray jpayloads, jintArray jlens, jint count) {

    if (session == 0) return nullptr;

    auto** payloads = new const uint8_t*[count];
    auto* lens = new size_t[count];
    jbyte** jbuffers = new jbyte*[count];

    for (int i = 0; i < count; i++) {
        jbyteArray arr = static_cast<jbyteArray>(env->GetObjectArrayElement(jpayloads, i));
        jbuffers[i] = env->GetByteArrayElements(arr, nullptr);
        payloads[i] = reinterpret_cast<const uint8_t*>(jbuffers[i]);
        lens[i] = static_cast<size_t>(env->GetArrayLength(arr));
        env->DeleteLocalRef(arr);
    }

    conduit_encode_result_t result;
    memset(&result, 0, sizeof(result));
    int err = conduit_encode_batch(
        reinterpret_cast<conduit_session_t*>(session),
        static_cast<uint64_t>(typeId),
        payloads, lens, static_cast<size_t>(count),
        &result);

    // Release
    for (int i = 0; i < count; i++) {
        jbyteArray arr = static_cast<jbyteArray>(env->GetObjectArrayElement(jpayloads, i));
        env->ReleaseByteArrayElements(arr, jbuffers[i], JNI_ABORT);
        env->DeleteLocalRef(arr);
    }
    delete[] payloads;
    delete[] lens;
    delete[] jbuffers;

    if (err != 0) return nullptr;

    jbyteArray jresult = env->NewByteArray(static_cast<jsize>(result.data_len));
    env->SetByteArrayRegion(jresult, 0, static_cast<jsize>(result.data_len),
                            reinterpret_cast<const jbyte*>(result.data));

    conduit_free_encode_result(&result);
    return jresult;
}

// ============================================================================
// Introspection
// ============================================================================

JNIEXPORT jstring JNICALL Java_io_conduit_JniCodecBinding_nSessionTypeName(
    JNIEnv* env, jclass, jlong session, jlong typeId) {

    if (session == 0) return env->NewStringUTF("");
    const char* name = conduit_session_type_name(
        reinterpret_cast<conduit_session_t*>(session),
        static_cast<uint64_t>(typeId));
    return env->NewStringUTF(name ? name : "");
}

JNIEXPORT jlong JNICALL Java_io_conduit_JniCodecBinding_nSessionLeafTypeCount(
    JNIEnv*, jclass, jlong session) {

    if (session == 0) return 0;
    return static_cast<jlong>(
        conduit_session_leaf_type_count(
            reinterpret_cast<conduit_session_t*>(session)));
}

JNIEXPORT jlongArray JNICALL Java_io_conduit_JniCodecBinding_nSessionLeafTypeIds(
    JNIEnv* env, jclass, jlong session) {

    if (session == 0) {
        return env->NewLongArray(0);
    }

    size_t count = conduit_session_leaf_type_count(
        reinterpret_cast<conduit_session_t*>(session));
    const uint64_t* ids = conduit_session_leaf_type_ids(
        reinterpret_cast<conduit_session_t*>(session));

    jlongArray result = env->NewLongArray(static_cast<jsize>(count));
    if (ids && count > 0) {
        // uint64_t and jlong are both 8 bytes; safe to cast
        env->SetLongArrayRegion(result, 0, static_cast<jsize>(count),
                                reinterpret_cast<const jlong*>(ids));
    }
    return result;
}

JNIEXPORT jint JNICALL Java_io_conduit_JniCodecBinding_nSessionIsReceiveOnly(
    JNIEnv*, jclass, jlong session, jlong typeId) {

    if (session == 0) return 0;
    return conduit_session_is_receive_only(
        reinterpret_cast<conduit_session_t*>(session),
        static_cast<uint64_t>(typeId));
}

JNIEXPORT jstring JNICALL Java_io_conduit_JniCodecBinding_nSessionProtocolName(
    JNIEnv* env, jclass, jlong session) {

    if (session == 0) return env->NewStringUTF("");
    const char* name = conduit_session_protocol_name(
        reinterpret_cast<conduit_session_t*>(session));
    return env->NewStringUTF(name ? name : "");
}

// ============================================================================
// Format message
// ============================================================================

JNIEXPORT jstring JNICALL Java_io_conduit_JniCodecBinding_nFormatMessage(
    JNIEnv* env, jclass, jlong session, jlong typeId,
    jbyteArray jpayload, jint payloadLen) {

    if (session == 0) return nullptr;

    jbyte* payload = env->GetByteArrayElements(jpayload, nullptr);
    char buf[4096];
    size_t written = 0;

    int err = conduit_format_message(
        reinterpret_cast<conduit_session_t*>(session),
        static_cast<uint64_t>(typeId),
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<size_t>(payloadLen),
        buf, sizeof(buf), &written);

    env->ReleaseByteArrayElements(jpayload, payload, JNI_ABORT);

    if (err != 0) return nullptr;
    buf[written < sizeof(buf) ? written : sizeof(buf) - 1] = '\0';
    return env->NewStringUTF(buf);
}

// ============================================================================
// Framing
// ============================================================================

JNIEXPORT jlong JNICALL Java_io_conduit_JniCodecBinding_nFramerCreate(
    JNIEnv*, jclass, jlong session) {

    if (session == 0) return 0;
    conduit_framer_t* framer = conduit_framer_create(
        reinterpret_cast<conduit_session_t*>(session));
    return reinterpret_cast<jlong>(framer);
}

JNIEXPORT void JNICALL Java_io_conduit_JniCodecBinding_nFramerDestroy(
    JNIEnv*, jclass, jlong framer) {

    if (framer == 0) return;
    conduit_framer_destroy(reinterpret_cast<conduit_framer_t*>(framer));
}

JNIEXPORT jobjectArray JNICALL Java_io_conduit_JniCodecBinding_nFramerFeed(
    JNIEnv* env, jclass, jlong framer, jbyteArray jdata, jint len) {

    if (framer == 0) return nullptr;

    jbyte* data = env->GetByteArrayElements(jdata, nullptr);

    conduit_frame_t* frames = nullptr;
    size_t count = 0;
    int err = conduit_framer_feed(
        reinterpret_cast<conduit_framer_t*>(framer),
        reinterpret_cast<const uint8_t*>(data),
        static_cast<size_t>(len),
        &frames, &count);

    env->ReleaseByteArrayElements(jdata, data, JNI_ABORT);

    if (err != 0 || count == 0) return nullptr;

    jclass byteArrayClass = env->FindClass("[B");
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), byteArrayClass, nullptr);
    env->DeleteLocalRef(byteArrayClass);

    for (size_t i = 0; i < count; i++) {
        jbyteArray jframe = env->NewByteArray(static_cast<jsize>(frames[i].data_len));
        if (frames[i].data && frames[i].data_len > 0) {
            env->SetByteArrayRegion(jframe, 0, static_cast<jsize>(frames[i].data_len),
                                    reinterpret_cast<const jbyte*>(frames[i].data));
        }
        env->SetObjectArrayElement(result, static_cast<jsize>(i), jframe);
        env->DeleteLocalRef(jframe);
    }

    conduit_free_frames(frames, count);
    return result;
}

// ============================================================================
// Version
// ============================================================================

JNIEXPORT jstring JNICALL Java_io_conduit_JniCodecBinding_nCodecVersion(JNIEnv* env, jclass) {
    const char* ver = conduit_codec_version();
    return env->NewStringUTF(ver ? ver : "");
}

} // extern "C"
