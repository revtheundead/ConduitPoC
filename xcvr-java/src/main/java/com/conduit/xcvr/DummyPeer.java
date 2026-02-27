package com.conduit.xcvr;

// Dummy ASTERIX Peer -- Standalone encode/decode roundtrip test (Java)
//
// Creates random ASTERIX messages using the asterix_alt (server) perspective,
// encodes them to bytes, decodes them back, and verifies the roundtrip.
//
// Usage: java DummyPeer [--count N] [--seed N]

import asterix_alt.*;

import java.util.Arrays;
import java.util.Random;

public class DummyPeer {

    public static void main(String[] args) {
        int count = 100;
        long seed = System.currentTimeMillis();

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--count" -> { if (i + 1 < args.length) count = Integer.parseInt(args[++i]); }
                case "--seed"  -> { if (i + 1 < args.length) seed = Long.parseLong(args[++i]); }
            }
        }

        System.out.printf("[dummy_peer] Starting encode/decode roundtrip test (count=%d, seed=%d)%n", count, seed);

        var rng = new Random(seed);

        int cat007DownlinkCount = 0;
        int cat021Count = 0;
        int cat048Count = 0;
        int cat253Count = 0;
        int successCount = 0;
        int failCount = 0;
        long totalBytes = 0;

        for (int i = 0; i < count; i++) {
            try {
                switch (rng.nextInt(4)) {
                    case 0 -> {
                        // Cat007 Downlink -- full record roundtrip
                        Cat007DownlinkRecord msg = RandomAsterixAlt.randomCat007Downlink(rng);
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
                        // Cat021 -- items-level roundtrip
                        Cat021RecordItems items = RandomAsterixAlt.randomCat021Items(rng);
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
                    case 2 -> {
                        // Cat048 -- items-level roundtrip
                        Cat048RecordItems items = RandomAsterixAlt.randomCat048Items(rng);
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
                    case 3 -> {
                        // Cat253 -- full record roundtrip
                        Cat253Record msg = RandomAsterixAlt.randomCat253(rng);
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
            }
        }

        System.out.println();
        System.out.println("[dummy_peer] ========== RESULTS ==========");
        System.out.printf("[dummy_peer] Total messages:      %d%n", count);
        System.out.printf("[dummy_peer]   Cat007 Downlink:   %d%n", cat007DownlinkCount);
        System.out.printf("[dummy_peer]   Cat021 Items:      %d%n", cat021Count);
        System.out.printf("[dummy_peer]   Cat048 Items:      %d%n", cat048Count);
        System.out.printf("[dummy_peer]   Cat253:            %d%n", cat253Count);
        System.out.printf("[dummy_peer] Roundtrip success:   %d%n", successCount);
        System.out.printf("[dummy_peer] Roundtrip failures:  %d%n", failCount);
        System.out.printf("[dummy_peer] Total bytes encoded: %d%n", totalBytes);
        System.out.println("[dummy_peer] ==============================");

        if (failCount > 0) {
            System.out.println("[dummy_peer] SOME TESTS FAILED!");
            System.exit(1);
        } else {
            System.out.println("[dummy_peer] All roundtrip tests passed.");
        }
    }
}
