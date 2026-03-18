// SPDX-License-Identifier: MIT
// Conduit - Java JNI codec benchmark
//
// Mirrors the C++ benchmark suite using JniCodecBinding through the JNI bridge.
// Requires: libconduit_codec_jni_bench on java.library.path
//
// Usage:
//   java -Djava.library.path=lib -cp <classpath> BenchJni

import io.conduit.JniCodecBinding;
import io.conduit.NativeCodecBinding;
import io.conduit.NativeCodecBinding.DecodedMessage;

public class BenchJni {

    private static final int STRESS_BATCH = 100_000;
    private static final int BATCH_SIZE = 10_000;
    private static final int WARMUP_ITERS = 100;
    private static final int BENCH_SAMPLES = 10;

    private final NativeCodecBinding binding;
    private final long stressSession;
    private final long sentrySession;

    private byte[][] wireData;
    private byte[][] fieldPayloads;
    private long[] typeIds;
    private long[] sessionForIdx;

    public BenchJni() {
        binding = new JniCodecBinding();
        stressSession = binding.sessionCreate("stress_frame");
        sentrySession = binding.sessionCreate("sentry_link");
        if (stressSession == 0 || sentrySession == 0) {
            throw new RuntimeException("Failed to create sessions");
        }
        sessionForIdx = new long[] {
            sentrySession, sentrySession, sentrySession, sentrySession,
            stressSession, stressSession, stressSession, stressSession
        };
    }

    public void bootstrap() {
        wireData = BenchWireData.buildAll();
        fieldPayloads = new byte[BenchWireData.COUNT][];
        typeIds = new long[BenchWireData.COUNT];
        for (int i = 0; i < BenchWireData.COUNT; i++) {
            DecodedMessage[] msgs = binding.decodeFrame(sessionForIdx[i], wireData[i]);
            if (msgs.length == 0) {
                throw new RuntimeException("Failed to decode " + BenchWireData.LABELS[i]);
            }
            typeIds[i] = msgs[0].typeId;
            fieldPayloads[i] = msgs[0].data;
        }
    }

    // ================================================================
    // Benchmark helper
    // ================================================================

    @FunctionalInterface
    interface BenchFn { void run(); }

    private static long bench(String name, BenchFn fn, int iterations, int samples) {
        for (int w = 0; w < Math.min(WARMUP_ITERS, 10); w++) fn.run();
        long[] times = new long[samples];
        for (int s = 0; s < samples; s++) {
            long t0 = System.nanoTime();
            fn.run();
            times[s] = System.nanoTime() - t0;
        }
        java.util.Arrays.sort(times);
        long median = times[samples / 2];
        if (iterations > 0) {
            long perIter = median / iterations;
            double totalMs = median / 1e6;
            double rate = (median > 0) ? (iterations / (median / 1e9)) : 0;
            System.out.printf("  %-45s %10.1f ms  (%8d ns/msg, %10.0f msgs/s)%n",
                name, totalMs, perIter, rate);
            return perIter;
        } else {
            System.out.printf("  %-45s %10d ns%n", name, median);
            return median;
        }
    }

    // ================================================================
    // Codec benchmarks
    // ================================================================

    public void runCodecEncode() {
        System.out.println("\nCodec: encode (Java generated codec)");
        System.out.println("-------------------------------------------");
        // Use generated Java encode (same as C++ uses generated C++ encode)
        Object[] msgObjs = {
            BenchWireData.makeHeartbeat(), BenchWireData.makeAlert(),
            BenchWireData.makeSensor(), BenchWireData.makeConfig(),
            BenchWireData.makePing(), BenchWireData.makeTelemetry(),
            BenchWireData.makeSurveillanceTypical(), BenchWireData.makeSurveillanceMaximal()
        };
        for (int i = 0; i < BenchWireData.COUNT; i++) {
            final int idx = i;
            if (idx < 4) {
                bench("encode " + BenchWireData.LABELS[i],
                    () -> BenchWireData.encodeSentry(msgObjs[idx]), 0, BENCH_SAMPLES);
            } else {
                bench("encode " + BenchWireData.LABELS[i],
                    () -> BenchWireData.encodeStress(msgObjs[idx]), 0, BENCH_SAMPLES);
            }
        }
    }

    public void runCodecDecode() {
        System.out.println("\nCodec: decode (Java JNI)");
        System.out.println("-------------------------------------------");
        for (int i = 0; i < BenchWireData.COUNT; i++) {
            final int idx = i;
            bench("decode " + BenchWireData.LABELS[i],
                () -> binding.decodeFrame(sessionForIdx[idx], wireData[idx]),
                0, BENCH_SAMPLES);
        }
    }

    public void runCodecRoundtrip() {
        System.out.println("\nCodec: roundtrip (Java encode → JNI decode)");
        System.out.println("-------------------------------------------");
        for (int i = 0; i < BenchWireData.COUNT; i++) {
            final int idx = i;
            bench("roundtrip " + BenchWireData.LABELS[i], () -> {
                byte[] wire = wireData[idx]; // pre-encoded
                binding.decodeFrame(sessionForIdx[idx], wire);
            }, 0, BENCH_SAMPLES);
        }
    }

    // ================================================================
    // Stress (100K)
    // ================================================================

    public void runStress() {
        int[] idxs = {BenchWireData.IDX_PING, BenchWireData.IDX_TELEMETRY,
                       BenchWireData.IDX_SURV_TYP, BenchWireData.IDX_SURV_MAX};

        System.out.println("\nStress: encode 100K (Java generated codec)");
        System.out.println("-------------------------------------------");
        Object[] stressMsgs = {BenchWireData.makePing(), BenchWireData.makeTelemetry(),
                                BenchWireData.makeSurveillanceTypical(), BenchWireData.makeSurveillanceMaximal()};
        for (int j = 0; j < idxs.length; j++) {
            final Object msg = stressMsgs[j];
            bench("100K " + BenchWireData.LABELS[idxs[j]] + " encode", () -> {
                for (int i = 0; i < STRESS_BATCH; i++)
                    BenchWireData.encodeStress(msg);
            }, STRESS_BATCH, 3);
        }

        System.out.println("\nStress: decode 100K (Java JNI)");
        System.out.println("-------------------------------------------");
        for (int idx : idxs) {
            bench("100K " + BenchWireData.LABELS[idx] + " decode", () -> {
                for (int i = 0; i < STRESS_BATCH; i++)
                    binding.decodeFrame(stressSession, wireData[idx]);
            }, STRESS_BATCH, 3);
        }

        System.out.println("\nStress: roundtrip 100K (Java encode → JNI decode)");
        System.out.println("-------------------------------------------");
        for (int j = 0; j < idxs.length; j++) {
            final int idx = idxs[j];
            final Object msg = stressMsgs[j];
            bench("100K " + BenchWireData.LABELS[idx] + " roundtrip", () -> {
                for (int i = 0; i < STRESS_BATCH; i++) {
                    byte[] w = BenchWireData.encodeStress(msg);
                    binding.decodeFrame(stressSession, w);
                }
            }, STRESS_BATCH, 3);
        }
    }

    // ================================================================
    // Mixed workload (100K)
    // ================================================================

    public void runMixed() {
        System.out.println("\nStress: mixed workload 100K (Java JNI)");
        System.out.println("-------------------------------------------");
        java.util.Random rng = new java.util.Random(42);
        int[] indices = new int[STRESS_BATCH];
        for (int i = 0; i < STRESS_BATCH; i++) {
            double r = rng.nextDouble();
            indices[i] = (r < 0.40) ? BenchWireData.IDX_PING :
                         (r < 0.75) ? BenchWireData.IDX_TELEMETRY :
                                      BenchWireData.IDX_SURV_TYP;
        }
        bench("100K mixed decode", () -> {
            for (int i = 0; i < STRESS_BATCH; i++)
                binding.decodeFrame(stressSession, wireData[indices[i]]);
        }, STRESS_BATCH, 3);
    }

    // ================================================================
    // Throughput (10K)
    // ================================================================

    public void runThroughput() {
        int[] idxs = {BenchWireData.IDX_HEARTBEAT, BenchWireData.IDX_ALERT,
                       BenchWireData.IDX_PING, BenchWireData.IDX_TELEMETRY};

        System.out.println("\nThroughput: batch encode 10K (Java generated codec)");
        System.out.println("-------------------------------------------");
        Object[] batchMsgs = {BenchWireData.makeHeartbeat(), BenchWireData.makeAlert(),
                               BenchWireData.makePing(), BenchWireData.makeTelemetry()};
        for (int j = 0; j < idxs.length; j++) {
            final int idx = idxs[j];
            final Object msg = batchMsgs[j];
            bench("10000x " + BenchWireData.LABELS[idx] + " encode", () -> {
                for (int i = 0; i < BATCH_SIZE; i++) {
                    if (idx < 4) BenchWireData.encodeSentry(msg);
                    else BenchWireData.encodeStress(msg);
                }
            }, BATCH_SIZE, 5);
        }

        System.out.println("\nThroughput: batch decode 10K (Java JNI)");
        System.out.println("-------------------------------------------");
        for (int idx : idxs) {
            bench("10000x " + BenchWireData.LABELS[idx] + " decode", () -> {
                for (int i = 0; i < BATCH_SIZE; i++)
                    binding.decodeFrame(sessionForIdx[idx], wireData[idx]);
            }, BATCH_SIZE, 5);
        }
    }

    public void close() {
        binding.sessionDestroy(stressSession);
        binding.sessionDestroy(sentrySession);
        binding.close();
    }

    // ================================================================
    // Main
    // ================================================================

    public static void main(String[] args) {
        System.out.println("============================================================");
        System.out.println("Conduit Java JNI Benchmark");
        System.out.println("============================================================");

        BenchJni b = new BenchJni();
        b.bootstrap();

        StringBuilder sb = new StringBuilder("Wire sizes: ");
        for (int i = 0; i < BenchWireData.COUNT; i++) {
            if (i > 0) sb.append(", ");
            sb.append(BenchWireData.LABELS[i]).append("=").append(b.wireData[i].length).append("B");
        }
        System.out.println(sb);

        b.runCodecEncode();
        b.runCodecDecode();
        b.runCodecRoundtrip();
        b.runStress();
        b.runMixed();
        b.runThroughput();
        b.close();

        System.out.println("\n============================================================");
        System.out.println("Java JNI benchmark complete.");
        System.out.println("============================================================");
    }
}
