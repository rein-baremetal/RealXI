// Thin JNI layer over the engine's C API (engine/include/realxi/api.h).
// Input, snapshot and event buffers are exposed to Kotlin as direct ByteBuffers, so the game loop allocates nothing per frame.
#include <jni.h>

#include "realxi/api.h"

#define RX_JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_realxi_engine_NativeBindings_##name

RX_JNI(jlong, nativeCreate)(JNIEnv*, jclass, jint seed, jint mode, jint drillId, jint difficulty, jint formationHome,
                            jint formationAway, jfloat halfSeconds, jint referee, jfloat assist, jint flags,
                            jint attempts, jint homeDifficulty) {
  return reinterpret_cast<jlong>(rx_create(static_cast<uint32_t>(seed), mode, drillId, difficulty, formationHome,
                                           formationAway, halfSeconds, referee, assist, flags, attempts, homeDifficulty));
}

RX_JNI(void, nativeDestroy)(JNIEnv*, jclass, jlong h) { rx_destroy(reinterpret_cast<void*>(h)); }

RX_JNI(jobject, nativeInputBuffer)(JNIEnv* env, jclass, jlong h) {
  return env->NewDirectByteBuffer(rx_input_buffer(reinterpret_cast<void*>(h)), 16 * sizeof(float));
}

RX_JNI(jobject, nativeSnapshotBuffer)(JNIEnv* env, jclass, jlong h) {
  return env->NewDirectByteBuffer(rx_snapshot_buffer(reinterpret_cast<void*>(h)), rx_snapshot_floats() * sizeof(float));
}

RX_JNI(jobject, nativeEventBuffer)(JNIEnv* env, jclass, jlong h) {
  return env->NewDirectByteBuffer(rx_event_buffer(reinterpret_cast<void*>(h)), rx::snap::kMaxEvents * 4 * sizeof(int32_t));
}

RX_JNI(jint, nativeEventCount)(JNIEnv*, jclass, jlong h) { return rx_event_count(reinterpret_cast<void*>(h)); }

RX_JNI(void, nativeStep)(JNIEnv*, jclass, jlong h, jfloat dt) { rx_step(reinterpret_cast<void*>(h), dt); }

RX_JNI(void, nativeCommand)(JNIEnv*, jclass, jlong h, jint cmd, jfloat arg) { rx_command(reinterpret_cast<void*>(h), cmd, arg); }

RX_JNI(jint, nativeSnapshotFloats)(JNIEnv*, jclass) { return rx_snapshot_floats(); }
