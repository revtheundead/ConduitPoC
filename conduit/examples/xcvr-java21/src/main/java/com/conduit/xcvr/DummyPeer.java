package com.conduit.xcvr;

// Dummy ASTERIX Peer -- TCP Server or Client mode (Java)
//
// Server mode: listens for connections, sends Cat007Downlink/Cat021/Cat048/Cat253
//              Uses asterix_alt session (server perspective: Downlink=send, Uplink=receive)
// Client mode: identical to PocApp (sends uplink types)
//              Uses asterix session (client perspective: Downlink=receive, Uplink=send)
//
// No raw data handling -- Conduit completely abstracts the codec layer.
// This is the Java equivalent of xcvr/src/dummy_peer.cpp.
//
// Usage:
//   java DummyPeer server [--port N] [--interval-ms N] [--session NAME]
//   java DummyPeer client [host] [port] [--interval-ms N] [--session NAME]

import asterix.*;
import asterix_alt.*;
import io.conduit.ConduitError;
import io.conduit.Transceiver;
import io.conduit.TransportConfig;

import java.util.Random;

public class DummyPeer {

    private static volatile boolean running = true;

    private static String stateName(Transceiver.ConnectionState state) {
        return state.name();
    }

    // ── Server-mode handlers (receives uplinks, asterix_alt perspective) ──

    private static void registerServerHandlers(Transceiver tx) {
        tx.onMessage(asterix_alt.Cat007UplinkRecord.class, (peer, msg) ->
                System.out.println("[RECV] Cat007UplinkRecord"));

        tx.onMessage(asterix_alt.Cat021Record.class, (peer, msg) ->
                System.out.println("[RECV] " + asterix_alt.Cat021Record.TYPE_NAME));

        tx.onMessage(asterix_alt.Cat048Record.class, (peer, msg) ->
                System.out.println("[RECV] " + asterix_alt.Cat048Record.TYPE_NAME));

        tx.onMessage(asterix_alt.Cat253Record.class, (peer, msg) ->
                System.out.println("[RECV] " + asterix_alt.Cat253Record.TYPE_NAME));
    }

    // ── Client-mode handlers (receives downlinks, asterix perspective) ───

    private static void registerClientHandlers(Transceiver tx) {
        tx.onMessage(asterix.Cat007DownlinkRecord.class, (peer, msg) ->
                System.out.println("[RECV] Cat007DownlinkRecord"));

        tx.onMessage(asterix.Cat021Record.class, (peer, msg) ->
                System.out.println("[RECV] " + asterix.Cat021Record.TYPE_NAME));

        tx.onMessage(asterix.Cat048Record.class, (peer, msg) ->
                System.out.println("[RECV] " + asterix.Cat048Record.TYPE_NAME));

        tx.onMessage(asterix.Cat253Record.class, (peer, msg) ->
                System.out.println("[RECV] " + asterix.Cat253Record.TYPE_NAME));
    }

    // ── Server sends: Cat007Downlink, Cat021, Cat048, Cat253 (asterix_alt) ──

    private static void sendServerMessage(Transceiver tx, Random rng) {
        try {
            switch (rng.nextInt(4)) {
                case 0 -> {
                    var msg = RandomAsterixAlt.randomCat007Downlink(rng);
                    System.out.println("[SEND] " + asterix_alt.Cat007DownlinkRecord.TYPE_NAME);
                    tx.send(msg);
                }
                case 1 -> {
                    var rec = new asterix_alt.Cat021Record();
                    rec.items = RandomAsterixAlt.randomCat021Items(rng);
                    System.out.println("[SEND] " + asterix_alt.Cat021Record.TYPE_NAME);
                    tx.send(rec);
                }
                case 2 -> {
                    var rec = new asterix_alt.Cat048Record();
                    rec.items = RandomAsterixAlt.randomCat048Items(rng);
                    System.out.println("[SEND] " + asterix_alt.Cat048Record.TYPE_NAME);
                    tx.send(rec);
                }
                case 3 -> {
                    var msg = RandomAsterixAlt.randomCat253(rng);
                    System.out.println("[SEND] " + asterix_alt.Cat253Record.TYPE_NAME);
                    tx.send(msg);
                }
            }
        } catch (ConduitError e) {
            handleSendError(e);
        } catch (Exception e) {
            System.err.printf("[SEND ERROR] %s%n", e.getMessage());
        }
    }

    // ── Client sends: Cat007Uplink, Cat021, Cat048, Cat253 (asterix) ────

    private static void sendClientMessage(Transceiver tx, int peerId, Random rng) {
        try {
            switch (rng.nextInt(4)) {
                case 0 -> {
                    var msg = RandomAsterix.randomCat007Uplink(rng);
                    System.out.println("[SEND] " + asterix.Cat007UplinkRecord.TYPE_NAME);
                    tx.send(peerId, msg);
                }
                case 1 -> {
                    var rec = new asterix.Cat021Record();
                    rec.items = RandomAsterix.randomCat021Items(rng);
                    System.out.println("[SEND] " + asterix.Cat021Record.TYPE_NAME);
                    tx.send(peerId, rec);
                }
                case 2 -> {
                    var rec = new asterix.Cat048Record();
                    rec.items = RandomAsterix.randomCat048Items(rng);
                    System.out.println("[SEND] " + asterix.Cat048Record.TYPE_NAME);
                    tx.send(peerId, rec);
                }
                case 3 -> {
                    var msg = RandomAsterix.randomCat253(rng);
                    System.out.println("[SEND] " + asterix.Cat253Record.TYPE_NAME);
                    tx.send(peerId, msg);
                }
            }
        } catch (ConduitError e) {
            handleSendError(e);
        } catch (Exception e) {
            System.err.printf("[SEND ERROR] %s%n", e.getMessage());
        }
    }

    // ── Stats ────────────────────────────────────────────────────────────

    private static void printStats(Transceiver tx) {
        var s = tx.stats();
        System.out.printf("[STATS] received=%d%n dispatched=%d%n dropped=%d%n"
                        + " decode_errors=%d%n handler_errors=%d%n handler_timeouts=%d%n"
                        + " bytes_rx=%d%n bytes_tx=%d%n%n",
                s.messagesReceived(), s.messagesDispatched(), s.messagesDropped(),
                s.decodeErrors(), s.handlerErrors(), s.handlerTimeouts(),
                s.bytesReceived(), s.bytesSent());
    }

    // ── Main ─────────────────────────────────────────────────────────────

    private static void printUsage() {
        System.err.println("Usage:");
        System.err.println("  java DummyPeer server [--port N] [--interval-ms N] [--session NAME] [--log-dir DIR] [--log-prefix PFX]");
        System.err.println("  java DummyPeer client [host] [port] [--interval-ms N] [--session NAME] [--log-dir DIR] [--log-prefix PFX]");
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

        Runtime.getRuntime().addShutdownHook(new Thread(() -> running = false));

        int intervalMs = 1000;
        String logDir = "./logs";
        String logPrefix = null;  // default set per mode below
        boolean logContent = true;

        // Parse common options
        for (int i = 1; i < args.length; i++) {
            if (args[i].equals("--interval-ms") && i + 1 < args.length) {
                intervalMs = Integer.parseInt(args[++i]);
            } else if (args[i].equals("--log-dir") && i + 1 < args.length) {
                logDir = args[++i];
            } else if (args[i].equals("--log-prefix") && i + 1 < args.length) {
                logPrefix = args[++i];
            } else if (args[i].equals("--no-content")) {
                logContent = false;
            }
        }

        if (isServer) {
            runServer(args, intervalMs, logDir, logPrefix != null ? logPrefix : "server", logContent);
        } else {
            runClient(args, intervalMs, logDir, logPrefix != null ? logPrefix : "client", logContent);
        }

        System.out.println("[dummy_peer] Done.");
    }

    private static void runServer(String[] args, int intervalMs, String logDir, String logPrefix, boolean logContent) {
        int port = 5000;
        String sessionName = "asterix_alt";

        for (int i = 1; i < args.length; i++) {
            switch (args[i]) {
                case "--port"    -> { if (i + 1 < args.length) port = Integer.parseInt(args[++i]); }
                case "--session" -> { if (i + 1 < args.length) sessionName = args[++i]; }
            }
        }

        System.out.printf("[dummy_peer] Server mode on port %d (interval=%dms, session=%s)%n",
                port, intervalMs, sessionName);

        try (var tx = new Transceiver()) {
            // Configure message logging (mirrors C++ cfg.message_log)
            var logCfg = new Transceiver.MessageLogConfig();
            logCfg.enabled = true;
            logCfg.mode    = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
            logCfg.output  = Transceiver.MessageLogOutput.FILE;
            logCfg.directory = logDir;
            logCfg.prefix    = logPrefix;
            logCfg.includeMessageContent = logContent;
            tx.setMessageLogConfig(logCfg);

            // Register the session (Java uses passthrough mode — codec runs in Java)
            tx.registerSession(sessionName, new asterix_alt.AsterixDataBlockSession());

            // TCP server peer (mirrors C++ TcpServerConfig{.bind_address="0.0.0.0", .port=port})
            tx.addPeer("clients", sessionName,
                    TransportConfig.tcpServer("0.0.0.0:" + port));

            registerServerHandlers(tx);

            tx.onStateChange((peer, newState) ->
                    System.out.printf("[STATE] peer=%d -> %s%n", peer, stateName(newState)));

            tx.onError((peer, peerName, errorCode, errorMsg) ->
                    System.err.printf("[ERROR] peer=%s code=%d %s%n",
                            peerName, errorCode, errorMsg));

            tx.start();
            System.out.println("[dummy_peer] Listening. Press Ctrl+C to stop.");

            var rng = new Random();
            while (running) {
                Thread.sleep(intervalMs);
                if (!running) break;
                sendServerMessage(tx, rng);
            }

            System.out.println("[dummy_peer] Stopping...");
            tx.stop();
            printStats(tx);

        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            System.err.printf("[ERROR] Failed to start: %s%n", e.getMessage());
            System.exit(1);
        }
    }

    private static void runClient(String[] args, int intervalMs, String logDir, String logPrefix, boolean logContent) {
        String host = "127.0.0.1";
        int port = 5000;
        String sessionName = "asterix";

        for (int i = 1; i < args.length; i++) {
            String arg = args[i];
            switch (arg) {
                case "--session" -> { if (i + 1 < args.length) sessionName = args[++i]; }
                default -> {
                    if (!arg.startsWith("--")) {
                        if (host.equals("127.0.0.1")) {
                            host = arg;
                        } else {
                            try { port = Integer.parseInt(arg); } catch (NumberFormatException ignored) {}
                        }
                    } else if (i + 1 < args.length) {
                        ++i;  // skip flag value
                    }
                }
            }
        }

        System.out.printf("[dummy_peer] Client mode connecting to %s:%d (interval=%dms, session=%s)%n",
                host, port, intervalMs, sessionName);

        try (var tx = new Transceiver()) {
            // Configure message logging (mirrors C++ cfg.message_log)
            var logCfg = new Transceiver.MessageLogConfig();
            logCfg.enabled = true;
            logCfg.mode    = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
            logCfg.output  = Transceiver.MessageLogOutput.FILE;
            logCfg.directory = logDir;
            logCfg.prefix    = logPrefix;
            logCfg.includeMessageContent = logContent;
            tx.setMessageLogConfig(logCfg);

            // Register the session (Java uses passthrough mode — codec runs in Java)
            tx.registerSession(sessionName, new asterix.AsterixDataBlockSession());

            // TCP client peer (mirrors C++ TcpClientConfig{.host=host, .port=port})
            int peerId = tx.addPeer("server", sessionName,
                    TransportConfig.tcpClient(host + ":" + port));

            registerClientHandlers(tx);

            tx.onStateChange((peer, newState) ->
                    System.out.printf("[STATE] peer=%d -> %s%n", peer, stateName(newState)));

            tx.onError((peer, peerName, errorCode, errorMsg) ->
                    System.err.printf("[ERROR] peer=%s code=%d %s%n",
                            peerName, errorCode, errorMsg));

            tx.start();
            System.out.println("[dummy_peer] Started. Press Ctrl+C to stop.");

            var rng = new Random();
            while (running) {
                Thread.sleep(intervalMs);
                if (!running) break;
                sendClientMessage(tx, peerId, rng);
            }

            System.out.println("[dummy_peer] Stopping...");
            tx.stop();
            printStats(tx);

        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            System.err.printf("[ERROR] Failed to start: %s%n", e.getMessage());
            System.exit(1);
        }
    }

    private static void handleSendError(ConduitError e) {
        switch (e.code()) {
            case -4 -> System.err.println("[SEND] no peers connected");
            case -5 -> System.err.printf("[SEND BLOCKED] %s%n", e.getMessage());
            case -6 -> System.err.printf("[SEND REJECTED] %s%n", e.getMessage());
            default -> System.err.printf("[SEND ERROR] %s%n", e.getMessage());
        }
    }
}
