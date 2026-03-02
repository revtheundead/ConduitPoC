#!/usr/bin/env python3
"""Dummy ASTERIX Peer -- TCP Server or Client mode (Python)

Server mode: listens for connections, sends Cat007Downlink/Cat021/Cat048/Cat253
             Uses asterix_alt session (server perspective: Downlink=send, Uplink=receive)
Client mode: identical to poc_app (sends uplink types)
             Uses asterix session (client perspective: Downlink=receive, Uplink=send)

No raw data handling -- Conduit completely abstracts the codec layer.
This is the Python equivalent of xcvr/src/dummy_peer.cpp.

Usage:
  python -m src.dummy_peer server [--port N] [--interval-ms N] [--session NAME]
  python -m src.dummy_peer client [host] [port] [--interval-ms N] [--session NAME]
"""

import sys
import os
import signal
import time
import random
import argparse

# Ensure both generated packages are importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'asterix'))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'asterix-alt'))

from conduit import Transceiver, TcpClientConfig, TcpServerConfig
from . import random_asterix
from . import random_asterix_alt

# Import generated message classes and sessions from both perspectives
# Client perspective (asterix) -- loaded from asterix/generated/
import generated.messages as asterix_msgs
from generated.sessions import AsterixDataBlockSession as AsterixSession
# Server perspective (asterix_alt) -- loaded by random_asterix_alt under generated_alt/

running = True


def signal_handler(sig, frame):
    global running
    running = False


# ── Helpers to load alt message classes ──────────────────────────────────

def _load_alt_messages():
    """Return the asterix-alt message classes.

    The ``generated_alt`` namespace is bootstrapped by ``random_asterix_alt``
    (which is imported above), so the module is already available.
    """
    return sys.modules['generated_alt.messages']


def _load_alt_session():
    """Return the asterix-alt AsterixDataBlockSession class."""
    return sys.modules['generated_alt.sessions'].AsterixDataBlockSession


# ── Handler registration ─────────────────────────────────────────────────

def register_server_handlers(tx, alt_msgs):
    """Register handlers for server mode (receives uplinks, asterix_alt)."""
    @tx.on(alt_msgs.Cat007UplinkRecord)
    def on_uplink(peer, msg):
        print("[RECV] Cat007UplinkRecord")

    @tx.on(alt_msgs.Cat021Record)
    def on_cat021(peer, msg):
        print(f"[RECV] {alt_msgs.Cat021Record.TYPE_NAME}")

    @tx.on(alt_msgs.Cat048Record)
    def on_cat048(peer, msg):
        print(f"[RECV] {alt_msgs.Cat048Record.TYPE_NAME}")

    @tx.on(alt_msgs.Cat253Record)
    def on_cat253(peer, msg):
        print(f"[RECV] {alt_msgs.Cat253Record.TYPE_NAME}")


def register_client_handlers(tx):
    """Register handlers for client mode (receives downlinks, asterix)."""
    @tx.on(asterix_msgs.Cat007DownlinkRecord)
    def on_downlink(peer, msg):
        print("[RECV] Cat007DownlinkRecord")

    @tx.on(asterix_msgs.Cat021Record)
    def on_cat021(peer, msg):
        print(f"[RECV] {asterix_msgs.Cat021Record.TYPE_NAME}")

    @tx.on(asterix_msgs.Cat048Record)
    def on_cat048(peer, msg):
        print(f"[RECV] {asterix_msgs.Cat048Record.TYPE_NAME}")

    @tx.on(asterix_msgs.Cat253Record)
    def on_cat253(peer, msg):
        print(f"[RECV] {asterix_msgs.Cat253Record.TYPE_NAME}")


# ── Send helpers ─────────────────────────────────────────────────────────

def send_server_message(tx, rng):
    """Server sends: Cat007Downlink, Cat021, Cat048, Cat253 (asterix_alt)."""
    try:
        choice = rng.randint(0, 3)
        if choice == 0:
            msg = random_asterix_alt.random_cat007_downlink(rng)
            print(f"[SEND] Cat007DownlinkRecord")
            tx.send(msg)
        elif choice == 1:
            msg = random_asterix_alt.random_cat021(rng)
            print(f"[SEND] Cat021Record")
            tx.send(msg)
        elif choice == 2:
            msg = random_asterix_alt.random_cat048(rng)
            print(f"[SEND] Cat048Record")
            tx.send(msg)
        elif choice == 3:
            msg = random_asterix_alt.random_cat253(rng)
            print(f"[SEND] Cat253Record")
            tx.send(msg)
    except Exception as e:
        print(f"[SEND ERROR] {e}", file=sys.stderr)


def send_client_message(tx, peer_id, rng):
    """Client sends: Cat007Uplink, Cat021, Cat048, Cat253 (asterix)."""
    try:
        choice = rng.randint(0, 3)
        if choice == 0:
            msg = random_asterix.random_cat007_uplink(rng)
            print(f"[SEND] {asterix_msgs.Cat007UplinkRecord.TYPE_NAME}")
            tx.send(peer_id, msg)
        elif choice == 1:
            msg = random_asterix.random_cat021(rng)
            print(f"[SEND] {asterix_msgs.Cat021Record.TYPE_NAME}")
            tx.send(peer_id, msg)
        elif choice == 2:
            msg = random_asterix.random_cat048(rng)
            print(f"[SEND] {asterix_msgs.Cat048Record.TYPE_NAME}")
            tx.send(peer_id, msg)
        elif choice == 3:
            msg = random_asterix.random_cat253(rng)
            print(f"[SEND] {asterix_msgs.Cat253Record.TYPE_NAME}")
            tx.send(peer_id, msg)
    except Exception as e:
        print(f"[SEND ERROR] {e}", file=sys.stderr)


# ── Stats ────────────────────────────────────────────────────────────────

def print_stats(tx):
    s = tx.stats()
    print(f"[STATS] received={s.messages_received}")
    print(f" dispatched={s.messages_dispatched}")
    print(f" dropped={s.messages_dropped}")
    print(f" decode_errors={s.decode_errors}")
    print(f" handler_errors={s.handler_errors}")
    print(f" handler_timeouts={s.handler_timeouts}")
    print(f" bytes_rx={s.bytes_received}")
    print(f" bytes_tx={s.bytes_sent}")
    print()


# ── Main ─────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Dummy ASTERIX Peer -- TCP Server or Client")
    parser.add_argument("mode", choices=["server", "client"],
                        help="Run as 'server' or 'client'")
    parser.add_argument("host", nargs="?", default="127.0.0.1",
                        help="Host (client mode, default: 127.0.0.1)")
    parser.add_argument("port", nargs="?", type=int, default=5000,
                        help="Port (default: 5000)")
    parser.add_argument("--interval-ms", type=int, default=1000,
                        help="Send interval in milliseconds (default: 1000)")
    parser.add_argument("--session", default=None,
                        help="Session name override")
    args = parser.parse_args()

    # Install signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    interval_s = args.interval_ms / 1000.0

    if args.mode == "server":
        run_server(args, interval_s)
    else:
        run_client(args, interval_s)

    print("[dummy_peer] Done.")


def run_server(args, interval_s):
    port = args.port
    session_name = args.session or "asterix_alt"

    print(f"[dummy_peer] Server mode on port {port} "
          f"(interval={args.interval_ms}ms, session={session_name})")

    alt_msgs = _load_alt_messages()

    with Transceiver() as tx:
        # Register the session (Python uses passthrough mode — codec runs in Python)
        AltSession = _load_alt_session()
        tx.register_session(session_name, AltSession())

        # TCP server peer (mirrors C++ TcpServerConfig{.bind_address="0.0.0.0", .port=port})
        tx.add_peer("clients", session_name,
                     TcpServerConfig(f"0.0.0.0:{port}"))

        register_server_handlers(tx, alt_msgs)

        tx.on_state_change(lambda peer, state:
            print(f"[STATE] peer={peer} -> {state}"))

        tx.on_error(lambda peer, peer_name, code, msg:
            print(f"[ERROR] peer={peer_name} code={code} {msg}",
                  file=sys.stderr))

        tx.start()
        print("[dummy_peer] Listening. Press Ctrl+C to stop.")

        rng = random.Random()
        while running:
            time.sleep(interval_s)
            if not running:
                break
            send_server_message(tx, rng)

        print("[dummy_peer] Stopping...")
        tx.stop()
        print_stats(tx)


def run_client(args, interval_s):
    host = args.host
    port = args.port
    session_name = args.session or "asterix"

    print(f"[dummy_peer] Client mode connecting to {host}:{port} "
          f"(interval={args.interval_ms}ms, session={session_name})")

    with Transceiver() as tx:
        # Register the session (Python uses passthrough mode — codec runs in Python)
        tx.register_session(session_name, AsterixSession())

        # TCP client peer (mirrors C++ TcpClientConfig{.host=host, .port=port})
        peer_id = tx.add_peer("server", session_name,
                              TcpClientConfig(f"{host}:{port}"))

        register_client_handlers(tx)

        tx.on_state_change(lambda peer, state:
            print(f"[STATE] peer={peer} -> {state}"))

        tx.on_error(lambda peer, peer_name, code, msg:
            print(f"[ERROR] peer={peer_name} code={code} {msg}",
                  file=sys.stderr))

        tx.start()
        print("[dummy_peer] Started. Press Ctrl+C to stop.")

        rng = random.Random()
        while running:
            time.sleep(interval_s)
            if not running:
                break
            send_client_message(tx, peer_id, rng)

        print("[dummy_peer] Stopping...")
        tx.stop()
        print_stats(tx)


if __name__ == "__main__":
    main()
