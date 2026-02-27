package com.conduit.xcvr;

// Dummy ASTERIX Peer — TCP Server or Client mode (Java)
//
// Server mode: listens for connections, sends Cat007Downlink/Cat021/Cat048/Cat253
//              Uses asterix_alt classes (server perspective: Downlink=send, Uplink=receive)
// Client mode: identical to PocApp (sends uplink types)
//              Uses asterix classes (client perspective: Downlink=receive, Uplink=send)
//
// Usage:
//   java DummyPeer server [--port N] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]
//   java DummyPeer client [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]

import conduit.transceiver.Transceiver;
import conduit.transceiver.TransceiverConfig;
import conduit.transceiver.MessageLogMode;
import conduit.transceiver.MessageLogOutput;
import conduit.transceiver.transport.TcpClientConfig;
import conduit.transceiver.transport.TcpServerConfig;

import java.util.Random;
import java.util.concurrent.atomic.AtomicBoolean;

public class DummyPeer {

    private static final AtomicBoolean running = new AtomicBoolean(true);

    // ── Handlers ────────────────────────────────────────────────────────────

    private static void registerServerHandlers(Transceiver tx) {
        tx.on(conduit.generated.asterix_alt.Cat007UplinkRecord.class, msg -> {
            System.out.println("[RECV] Cat007UplinkRecord");
        });
        tx.on(conduit.generated.asterix_alt.Cat021Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
        tx.on(conduit.generated.asterix_alt.Cat048Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
        tx.on(conduit.generated.asterix_alt.Cat253Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
    }

    private static void registerClientHandlers(Transceiver tx) {
        tx.on(conduit.generated.asterix.Cat007DownlinkRecord.class, msg -> {
            System.out.println("[RECV] Cat007DownlinkRecord");
        });
        tx.on(conduit.generated.asterix.Cat021Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
        tx.on(conduit.generated.asterix.Cat048Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
        tx.on(conduit.generated.asterix.Cat253Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
    }

    // ── Send helpers ────────────────────────────────────────────────────────

    private static void sendServerMessage(Transceiver tx, Random rng) {
        var sendResult = switch (rng.nextInt(4)) {
            case 0 -> {
                var msg = RandomAsterixAlt.randomCat007Downlink(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            case 1 -> {
                var msg = RandomAsterixAlt.randomCat021(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            case 2 -> {
                var msg = RandomAsterixAlt.randomCat048(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            case 3 -> {
                var msg = RandomAsterixAlt.randomCat253(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            default -> throw new IllegalStateException();
        };
        reportSendResult(sendResult);
    }

    private static void sendClientMessage(Transceiver tx, Random rng) {
        var sendResult = switch (rng.nextInt(4)) {
            case 0 -> {
                var msg = RandomAsterix.randomCat007Uplink(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            case 1 -> {
                var msg = RandomAsterix.randomCat021(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            case 2 -> {
                var msg = RandomAsterix.randomCat048(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            case 3 -> {
                var msg = RandomAsterix.randomCat253(rng);
                System.out.println("[SEND] " + msg.TYPE_NAME);
                yield tx.send(msg);
            }
            default -> throw new IllegalStateException();
        };
        reportSendResult(sendResult);
    }

    private static void reportSendResult(conduit.VoidResult result) {
        if (!result.isOk()) {
            var code = result.error().code();
            if (code == conduit.ErrorCode.DIRECTION_VIOLATION)
                System.err.println("[SEND BLOCKED] " + result.error().formatShort());
            else if (code == conduit.ErrorCode.ENCODE_CONSTRAINT_VIOLATION)
                System.err.println("[SEND REJECTED] " + result.error().formatShort());
            else
                System.err.println("[SEND ERROR] " + result.error().formatShort());
        }
    }

    // ── Stats ───────────────────────────────────────────────────────────────

    private static void printStats(Transceiver tx) {
        var s = tx.stats().snapshot();
        System.out.printf("""
                [STATS] received=%d
                 dispatched=%d
                 dropped=%d
                 decode_errors=%d
                 handler_errors=%d
                 handler_timeouts=%d
                 bytes_rx=%d
                 bytes_tx=%d

                """,
                s.messagesReceived(), s.messagesDispatched(), s.messagesDropped(),
                s.decodeErrors(), s.handlerErrors(), s.handlerTimeouts(),
                s.bytesReceived(), s.bytesSent());
    }

    // ── Main ────────────────────────────────────────────────────────────────

    private static void printUsage() {
        System.err.println("""
                Usage:
                  dummy_peer server [--port N] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]
                  dummy_peer client [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]""");
    }

    public static void main(String[] args) {
        if (args.length < 1) {
            printUsage();
            System.exit(1);
        }

        String mode = args[0];
        boolean isServer = mode.equals("server");
        boolean isClient = mode.equals("client");

        if (!isServer && !isClient) {
            System.err.println("Unknown mode: " + mode);
            printUsage();
            System.exit(1);
        }

        // Install shutdown hook
        Runtime.getRuntime().addShutdownHook(new Thread(() -> running.set(false)));

        int intervalMs = 1000;
        String logDir = "./logs";
        String logPrefix = "";

        // Parse common options
        for (int i = 1; i < args.length; i++) {
            switch (args[i]) {
                case "--interval-ms" -> { if (i + 1 < args.length) intervalMs = Integer.parseInt(args[++i]); }
                case "--log-dir"     -> { if (i + 1 < args.length) logDir = args[++i]; }
                case "--log-prefix"  -> { if (i + 1 < args.length) logPrefix = args[++i]; }
            }
        }

        if (isServer) {
            runServer(args, intervalMs, logDir, logPrefix);
        } else {
            runClient(args, intervalMs, logDir, logPrefix);
        }

        System.out.println("[dummy_peer] Done.");
    }

    private static void runServer(String[] args, int intervalMs, String logDir, String logPrefix) {
        int port = 5000;
        for (int i = 1; i < args.length; i++) {
            if (args[i].equals("--port") && i + 1 < args.length) {
                port = Integer.parseInt(args[++i]);
            }
        }
        if (logPrefix.isEmpty()) logPrefix = "server";

        System.out.printf("[dummy_peer] Server mode on port %d (interval=%dms)%n", port, intervalMs);

        var cfg = new TransceiverConfig();
        cfg.messageLog().setEnabled(true);
        cfg.messageLog().setMode(MessageLogMode.SEPARATE_DIRECTION);
        cfg.messageLog().setOutput(MessageLogOutput.FILE);
        cfg.messageLog().setDirectory(logDir);
        cfg.messageLog().setPrefix(logPrefix);
        cfg.addPeer("clients",
                conduit.generated.asterix_alt.AsterixAltSession::createAsterixDataBlockSession,
                new TcpServerConfig("0.0.0.0", port));

        var tx = new Transceiver(cfg);
        registerServerHandlers(tx);

        tx.onStateChange((peerId, state) -> {
            System.out.printf("[STATE] peer=%d -> %s%n", peerId.value(), state);
        });
        tx.onError(event -> {
            System.err.printf("[ERROR] peer=%s %s%n",
                    event.peerName(), event.error().formatShort());
        });

        var result = tx.start();
        if (!result.isOk()) {
            System.err.println("[ERROR] Failed to start: " + result.error().formatShort());
            System.exit(1);
        }

        System.out.println("[dummy_peer] Listening. Press Ctrl+C to stop.");

        var rng = new Random();
        while (running.get()) {
            try {
                Thread.sleep(intervalMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
            if (!running.get()) break;
            sendServerMessage(tx, rng);
        }

        System.out.println("[dummy_peer] Stopping...");
        tx.stop();
        printStats(tx);
    }

    private static void runClient(String[] args, int intervalMs, String logDir, String logPrefix) {
        String host = "127.0.0.1";
        int port = 5000;
        if (logPrefix.isEmpty()) logPrefix = "client";

        // Parse positional args (host, port) — skip flag args
        boolean hostSet = false;
        boolean portSet = false;
        for (int i = 1; i < args.length; i++) {
            if (args[i].startsWith("--")) {
                i++; // skip flag value
                continue;
            }
            if (!hostSet) {
                host = args[i];
                hostSet = true;
            } else if (!portSet) {
                port = Integer.parseInt(args[i]);
                portSet = true;
            }
        }

        System.out.printf("[dummy_peer] Client mode connecting to %s:%d (interval=%dms)%n",
                host, port, intervalMs);

        var cfg = new TransceiverConfig();
        cfg.messageLog().setEnabled(true);
        cfg.messageLog().setMode(MessageLogMode.SEPARATE_DIRECTION);
        cfg.messageLog().setOutput(MessageLogOutput.FILE);
        cfg.messageLog().setDirectory(logDir);
        cfg.messageLog().setPrefix(logPrefix);
        cfg.addPeer("server",
                conduit.generated.asterix.AsterixSession::createAsterixDataBlockSession,
                new TcpClientConfig(host, port));

        var tx = new Transceiver(cfg);
        registerClientHandlers(tx);

        tx.onStateChange((peerId, state) -> {
            System.out.printf("[STATE] peer=%d -> %s%n", peerId.value(), state);
        });
        tx.onError(event -> {
            System.err.printf("[ERROR] peer=%s %s%n",
                    event.peerName(), event.error().formatShort());
        });

        var result = tx.start();
        if (!result.isOk()) {
            System.err.println("[ERROR] Failed to start: " + result.error().formatShort());
            System.exit(1);
        }

        System.out.println("[dummy_peer] Started. Press Ctrl+C to stop.");

        var rng = new Random();
        while (running.get()) {
            try {
                Thread.sleep(intervalMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
            if (!running.get()) break;
            sendClientMessage(tx, rng);
        }

        System.out.println("[dummy_peer] Stopping...");
        tx.stop();
        printStats(tx);
    }
}
