package com.conduit.xcvr;

// PoC ASTERIX Standalone Test Application (Java)
//
// Creates random ASTERIX messages using the asterix (client) perspective,
// encodes them to bytes, decodes them back, and verifies the roundtrip.
//
// Usage: java PocApp [--count N] [--seed N]

import asterix.*;

import java.util.Arrays;
import java.util.Random;

public class PocApp {

    public static void main(String[] args) {
        int count = 100;
        long seed = System.currentTimeMillis();

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--count" -> { if (i + 1 < args.length) count = Integer.parseInt(args[++i]); }
                case "--seed"  -> { if (i + 1 < args.length) seed = Long.parseLong(args[++i]); }
            }
        }

        System.out.printf("[poc_app] Starting encode/decode roundtrip test (count=%d, seed=%d)%n", count, seed);

        var rng = new Random(seed);

        int cat007DownlinkCount = 0;
        int cat007UplinkCount = 0;
        int cat021Count = 0;
        int cat048Count = 0;
        int cat253Count = 0;
        int successCount = 0;
        int failCount = 0;
        long totalBytes = 0;

        for (int i = 0; i < count; i++) {
            try {
                switch (rng.nextInt(5)) {
                    case 0 -> {
                        // Cat007 Downlink -- full record roundtrip
                        Cat007DownlinkRecord msg = RandomAsterix.randomCat007Downlink(rng);
                        byte[] encoded = msg.encodeBytes();
                        Cat007DownlinkRecord decoded = Cat007DownlinkRecord.decodeBytes(encoded);
                        byte[] reencoded = decoded.encodeBytes();
                        if (Arrays.equals(encoded, reencoded)) {
                            successCount++;
                        } else {
                            failCount++;
                            System.err.printf("[FAIL] Cat007Downlink #%d: roundtrip mismatch (encoded=%d bytes, reencoded=%d bytes)%n",
                                    i, encoded.length, reencoded.length);
                        }
                        totalBytes += encoded.length;
                        cat007DownlinkCount++;
                    }
                    case 1 -> {
                        // Cat007 Uplink -- full record roundtrip
                        Cat007UplinkRecord msg = RandomAsterix.randomCat007Uplink(rng);
                        byte[] encoded = msg.encodeBytes();
                        Cat007UplinkRecord decoded = Cat007UplinkRecord.decodeBytes(encoded);
                        byte[] reencoded = decoded.encodeBytes();
                        if (Arrays.equals(encoded, reencoded)) {
                            successCount++;
                        } else {
                            failCount++;
                            System.err.printf("[FAIL] Cat007Uplink #%d: roundtrip mismatch (encoded=%d bytes, reencoded=%d bytes)%n",
                                    i, encoded.length, reencoded.length);
                        }
                        totalBytes += encoded.length;
                        cat007UplinkCount++;
                    }
                    case 2 -> {
                        // Cat021 -- items-level roundtrip (Record wrapper has wrong items type)
                        Cat021RecordItems items = RandomAsterix.randomCat021Items(rng);
                        byte[] encoded = items.encodeBytes();
                        Cat021RecordItems decoded = Cat021RecordItems.decodeBytes(encoded);
                        byte[] reencoded = decoded.encodeBytes();
                        if (Arrays.equals(encoded, reencoded)) {
                            successCount++;
                        } else {
                            failCount++;
                            System.err.printf("[FAIL] Cat021 #%d: roundtrip mismatch (encoded=%d bytes, reencoded=%d bytes)%n",
                                    i, encoded.length, reencoded.length);
                        }
                        totalBytes += encoded.length;
                        cat021Count++;
                    }
                    case 3 -> {
                        // Cat048 -- items-level roundtrip (Record wrapper has wrong items type)
                        Cat048RecordItems items = RandomAsterix.randomCat048Items(rng);
                        byte[] encoded = items.encodeBytes();
                        Cat048RecordItems decoded = Cat048RecordItems.decodeBytes(encoded);
                        byte[] reencoded = decoded.encodeBytes();
                        if (Arrays.equals(encoded, reencoded)) {
                            successCount++;
                        } else {
                            failCount++;
                            System.err.printf("[FAIL] Cat048 #%d: roundtrip mismatch (encoded=%d bytes, reencoded=%d bytes)%n",
                                    i, encoded.length, reencoded.length);
                        }
                        totalBytes += encoded.length;
                        cat048Count++;
                    }
                    case 4 -> {
                        // Cat253 -- full record roundtrip
                        Cat253Record msg = RandomAsterix.randomCat253(rng);
                        byte[] encoded = msg.encodeBytes();
                        Cat253Record decoded = Cat253Record.decodeBytes(encoded);
                        byte[] reencoded = decoded.encodeBytes();
                        if (Arrays.equals(encoded, reencoded)) {
                            successCount++;
                        } else {
                            failCount++;
                            System.err.printf("[FAIL] Cat253 #%d: roundtrip mismatch (encoded=%d bytes, reencoded=%d bytes)%n",
                                    i, encoded.length, reencoded.length);
                        }
                        totalBytes += encoded.length;
                        cat253Count++;
                    }
                }
            } catch (Exception e) {
                failCount++;
                System.err.printf("[ERROR] Message #%d threw exception: %s%n", i, e.getMessage());
                e.printStackTrace(System.err);
            }
        }

        System.out.println();
        System.out.println("[poc_app] ========== RESULTS ==========");
        System.out.printf("[poc_app] Total messages:      %d%n", count);
        System.out.printf("[poc_app]   Cat007 Downlink:   %d%n", cat007DownlinkCount);
        System.out.printf("[poc_app]   Cat007 Uplink:     %d%n", cat007UplinkCount);
        System.out.printf("[poc_app]   Cat021 Items:      %d%n", cat021Count);
        System.out.printf("[poc_app]   Cat048 Items:      %d%n", cat048Count);
        System.out.printf("[poc_app]   Cat253:            %d%n", cat253Count);
        System.out.printf("[poc_app] Roundtrip success:   %d%n", successCount);
        System.out.printf("[poc_app] Roundtrip failures:  %d%n", failCount);
        System.out.printf("[poc_app] Total bytes encoded: %d%n", totalBytes);
        System.out.println("[poc_app] ==============================");

        if (failCount > 0) {
            System.out.println("[poc_app] SOME TESTS FAILED!");
            System.exit(1);
        } else {
            System.out.println("[poc_app] All roundtrip tests passed.");
        }
    }
}
