package com.conduit.xcvr;

// PoC ASTERIX Transceiver Application — TCP Client (Java)
//
// Connects to a peer, sends random Cat007Uplink/Cat021/Cat048/Cat253 messages,
// and logs all received messages to stdout.
//
// Usage: java PocApp [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]

import conduit.transceiver.Transceiver;
import conduit.transceiver.TransceiverConfig;
import conduit.transceiver.MessageLogMode;
import conduit.transceiver.MessageLogOutput;
import conduit.transceiver.transport.TcpClientConfig;
import conduit.generated.asterix.*;

import java.util.Random;
import java.util.concurrent.atomic.AtomicBoolean;

public class PocApp {

    private static final AtomicBoolean running = new AtomicBoolean(true);

    public static void main(String[] args) {
        // Parse args
        String host = "127.0.0.1";
        int port = 5000;
        int intervalMs = 1000;
        String logDir = "./logs";
        String logPrefix = "poc";

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--interval-ms" -> { if (i + 1 < args.length) intervalMs = Integer.parseInt(args[++i]); }
                case "--log-dir"     -> { if (i + 1 < args.length) logDir = args[++i]; }
                case "--log-prefix"  -> { if (i + 1 < args.length) logPrefix = args[++i]; }
                default -> {
                    if (i == 0 && !args[i].startsWith("--")) {
                        host = args[i];
                    } else if (i == 1 && !args[i].startsWith("--")) {
                        port = Integer.parseInt(args[i]);
                    }
                }
            }
        }

        // Install shutdown hook (Ctrl+C)
        Runtime.getRuntime().addShutdownHook(new Thread(() -> running.set(false)));

        System.out.printf("[poc_app] Connecting to %s:%d (interval=%dms)%n", host, port, intervalMs);

        // Build transceiver config
        var cfg = new TransceiverConfig();
        cfg.messageLog().setEnabled(true);
        cfg.messageLog().setMode(MessageLogMode.SEPARATE_DIRECTION);
        cfg.messageLog().setOutput(MessageLogOutput.FILE);
        cfg.messageLog().setDirectory(logDir);
        cfg.messageLog().setPrefix(logPrefix);
        cfg.addPeer("server",
                AsterixSession::createAsterixDataBlockSession,
                new TcpClientConfig(host, port));

        var tx = new Transceiver(cfg);

        // Register typed receive handlers
        tx.on(Cat007DownlinkRecord.class, msg -> {
            System.out.println("[RECV] Cat007DownlinkRecord");
        });
        tx.on(Cat021Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
        tx.on(Cat048Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });
        tx.on(Cat253Record.class, msg -> {
            System.out.println("[RECV] " + msg.TYPE_NAME);
        });

        // Connection state logging
        tx.onStateChange((peerId, state) -> {
            System.out.printf("[STATE] peer=%d -> %s%n", peerId.value(), state);
        });

        // Structured error reporting
        tx.onError(event -> {
            System.err.printf("[ERROR] peer=%s %s%n",
                    event.peerName(), event.error().formatShort());
        });

        // Start
        var result = tx.start();
        if (!result.isOk()) {
            System.err.println("[ERROR] Failed to start: " + result.error().formatShort());
            System.exit(1);
        }

        System.out.println("[poc_app] Started. Press Ctrl+C to stop.");

        // Send loop
        var rng = new Random();

        while (running.get()) {
            try {
                Thread.sleep(intervalMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
            if (!running.get()) break;

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

            if (!sendResult.isOk()) {
                var code = sendResult.error().code();
                if (code == conduit.ErrorCode.DIRECTION_VIOLATION)
                    System.err.println("[SEND BLOCKED] " + sendResult.error().formatShort());
                else if (code == conduit.ErrorCode.ENCODE_CONSTRAINT_VIOLATION)
                    System.err.println("[SEND REJECTED] " + sendResult.error().formatShort());
                else
                    System.err.println("[SEND ERROR] " + sendResult.error().formatShort());
            }
        }

        System.out.println("[poc_app] Stopping...");
        tx.stop();

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

        System.out.println("[poc_app] Done.");
    }
}
