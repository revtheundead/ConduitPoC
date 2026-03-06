package com.conduit.xcvr;

// PoC ASTERIX Transceiver Application -- TCP Client (Java)
//
// Connects to a dummy_peer server, sends random Cat007Uplink/Cat021/Cat048/
// Cat253 messages, and logs all received messages.  Uses the Conduit
// Transceiver to exercise the full stack: transport, codec, handlers, callbacks.
// No raw data handling -- Conduit completely abstracts the codec layer.
//
// This is the Java equivalent of xcvr/src/poc_app.cpp.
//
// Usage: java PocApp [host] [port] [--interval-ms N] [--session NAME]

import asterix.*;
import io.conduit.ConduitError;
import io.conduit.Transceiver;
import io.conduit.TransportConfig;

import java.util.Random;

public class PocApp {

    private static volatile boolean running = true;

    public static void main(String[] args) {
        String host = "127.0.0.1";
        int port = 5000;
        int intervalMs = 1000;
        String sessionName = "asterix";

        // Parse arguments (mirrors C++ poc_app arg parsing)
        String logDir = "./logs";
        String logPrefix = "poc";

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--interval-ms" -> { if (i + 1 < args.length) intervalMs = Integer.parseInt(args[++i]); }
                case "--session"     -> { if (i + 1 < args.length) sessionName = args[++i]; }
                case "--log-dir"     -> { if (i + 1 < args.length) logDir = args[++i]; }
                case "--log-prefix"  -> { if (i + 1 < args.length) logPrefix = args[++i]; }
                default -> {
                    if (!args[i].startsWith("--")) {
                        if (host.equals("127.0.0.1") && i == 0) {
                            host = args[i];
                        } else {
                            try { port = Integer.parseInt(args[i]); } catch (NumberFormatException ignored) {}
                        }
                    }
                }
            }
        }

        // Install shutdown hook for Ctrl+C (mirrors C++ signal handler)
        Runtime.getRuntime().addShutdownHook(new Thread(() -> running = false));

        System.out.printf("[poc_app] Connecting to %s:%d (interval=%dms, session=%s)%n",
                host, port, intervalMs, sessionName);

        try (var tx = new Transceiver()) {
            // Configure message logging (mirrors C++ cfg.message_log)
            var logCfg = new Transceiver.MessageLogConfig();
            logCfg.enabled = true;
            logCfg.mode = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
            logCfg.output = Transceiver.MessageLogOutput.FILE;
            logCfg.directory = logDir;
            logCfg.prefix = logPrefix;
            logCfg.includeMessageContent = false;
            tx.setMessageLogConfig(logCfg);

            // Register the session (Java uses passthrough mode — codec runs in Java)
            tx.registerSession(sessionName, new AsterixDataBlockSession());

            // Add TCP client peer (mirrors C++ cfg.add_peer("server", ..., TcpClientConfig))
            int peerId = tx.addPeer("server", sessionName,
                    TransportConfig.tcpClient(host + ":" + port));

            // ── Typed message handlers (mirrors C++ tx.on<T>()) ──────────

            tx.onMessage(Cat007DownlinkRecord.class, (peer, msg) ->
                    System.out.println("[RECV] Cat007DownlinkRecord"));

            tx.onMessage(Cat021Record.class, (peer, msg) ->
                    System.out.println("[RECV] " + Cat021Record.TYPE_NAME));

            tx.onMessage(Cat048Record.class, (peer, msg) ->
                    System.out.println("[RECV] " + Cat048Record.TYPE_NAME));

            tx.onMessage(Cat253Record.class, (peer, msg) ->
                    System.out.println("[RECV] " + Cat253Record.TYPE_NAME));

            // ── State change callback (mirrors C++ tx.on_state_change()) ─

            tx.onStateChange((peer, newState) ->
                    System.out.printf("[STATE] peer=%d -> %d%n", peer, newState));

            // ── Error callback (mirrors C++ tx.on_error()) ───────────────

            tx.onError((peer, peerName, errorCode, errorMsg) ->
                    System.err.printf("[ERROR] peer=%s code=%d %s%n",
                            peerName, errorCode, errorMsg));

            // ── Start ────────────────────────────────────────────────────

            tx.start();
            System.out.println("[poc_app] Started. Press Ctrl+C to stop.");

            // ── Send loop (mirrors C++ main loop with random message selection) ──

            var rng = new Random();

            while (running) {
                Thread.sleep(intervalMs);
                if (!running) break;

                try {
                    switch (rng.nextInt(4)) {
                        case 0 -> {
                            var msg = RandomAsterix.randomCat007Uplink(rng);
                            System.out.println("[SEND] " + Cat007UplinkRecord.TYPE_NAME);
                            tx.send(peerId, msg);
                        }
                        case 1 -> {
                            var rec = new Cat021Record();
                            rec.items = RandomAsterix.randomCat021Items(rng);
                            System.out.println("[SEND] " + Cat021Record.TYPE_NAME);
                            tx.send(peerId, rec);
                        }
                        case 2 -> {
                            var rec = new Cat048Record();
                            rec.items = RandomAsterix.randomCat048Items(rng);
                            System.out.println("[SEND] " + Cat048Record.TYPE_NAME);
                            tx.send(peerId, rec);
                        }
                        case 3 -> {
                            var msg = RandomAsterix.randomCat253(rng);
                            System.out.println("[SEND] " + Cat253Record.TYPE_NAME);
                            tx.send(peerId, msg);
                        }
                    }
                } catch (ConduitError e) {
                    if (e.code() != -4) // -4 = no peer connected
                        System.err.printf("[SEND ERROR] %s%n", e.getMessage());
                } catch (Exception e) {
                    System.err.printf("[SEND ERROR] %s%n", e.getMessage());
                }
            }

            // ── Stop & stats (mirrors C++ tx.stop() + tx.stats().snapshot()) ──

            System.out.println("[poc_app] Stopping...");
            tx.stop();

            var s = tx.stats();
            System.out.printf("[STATS] received=%d%n dispatched=%d%n dropped=%d%n"
                            + " decode_errors=%d%n handler_errors=%d%n handler_timeouts=%d%n"
                            + " bytes_rx=%d%n bytes_tx=%d%n%n",
                    s.messagesReceived(), s.messagesDispatched(), s.messagesDropped(),
                    s.decodeErrors(), s.handlerErrors(), s.handlerTimeouts(),
                    s.bytesReceived(), s.bytesSent());

        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            System.err.printf("[ERROR] %s%n", e.getMessage());
            System.exit(1);
        }

        System.out.println("[poc_app] Done.");
    }
}
