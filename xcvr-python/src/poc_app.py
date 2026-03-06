#!/usr/bin/env python3
"""PoC ASTERIX Transceiver Application -- TCP Client (Python)

Connects to a dummy_peer server, sends random Cat007Uplink/Cat021/Cat048/
Cat253 messages, and logs all received messages.  Uses the Conduit
Transceiver to exercise the full stack: transport, codec, handlers, callbacks.
No raw data handling -- Conduit completely abstracts the codec layer.

This is the Python equivalent of xcvr/src/poc_app.cpp.

Usage: python -m src.poc_app [host] [port] [--interval-ms N] [--session NAME]
"""

import sys
import os
import signal
import time
import random
import argparse
import traceback

# Ensure the asterix generated package is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'asterix'))

from conduit import Transceiver, TcpClientConfig, MessageLogMode, MessageLogOutput, ConduitError
from . import random_asterix

# Import generated message classes and session for handler registration
from generated.messages import (
    Cat007DownlinkRecord,
    Cat007UplinkRecord,
    Cat021Record,
    Cat048Record,
    Cat253Record,
)
from generated.sessions import AsterixDataBlockSession

running = True

_STATE_NAMES = {
    0: "Disconnected",
    1: "Connecting",
    2: "Connected",
    3: "Reconnecting",
    4: "Failed",
}

def _state_name(state):
    return _STATE_NAMES.get(state, f"Unknown({state})")


def signal_handler(sig, frame):
    global running
    running = False


def main():
    parser = argparse.ArgumentParser(
        description="PoC ASTERIX Transceiver -- TCP Client")
    parser.add_argument("host", nargs="?", default="127.0.0.1",
                        help="Server host (default: 127.0.0.1)")
    parser.add_argument("port", nargs="?", type=int, default=5000,
                        help="Server port (default: 5000)")
    parser.add_argument("--interval-ms", type=int, default=1000,
                        help="Send interval in milliseconds (default: 1000)")
    parser.add_argument("--session", default="asterix",
                        help="Session name (default: asterix)")
    parser.add_argument("--log-dir", default="./logs",
                        help="Message log directory (default: ./logs)")
    parser.add_argument("--log-prefix", default="poc",
                        help="Message log file prefix (default: poc)")
    args = parser.parse_args()

    # Install signal handlers (mirrors C++ signal_handler)
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    print(f"[poc_app] Connecting to {args.host}:{args.port} "
          f"(interval={args.interval_ms}ms, session={args.session})")

    with Transceiver() as tx:
        # Configure message logging (mirrors C++ cfg.message_log)
        tx.set_message_log_config(
            enabled=True,
            mode=MessageLogMode.SEPARATE_DIRECTION,
            output=MessageLogOutput.FILE,
            directory=args.log_dir,
            prefix=args.log_prefix,
        )

        # Register the session (Python uses passthrough mode — codec runs in Python)
        tx.register_session(args.session, AsterixDataBlockSession())

        # Add TCP client peer (mirrors C++ cfg.add_peer("server", ..., TcpClientConfig))
        peer_id = tx.add_peer("server", args.session,
                              TcpClientConfig(f"{args.host}:{args.port}"))

        # ── Typed message handlers (mirrors C++ tx.on<T>()) ──────────

        @tx.on(Cat007DownlinkRecord)
        def on_cat007_downlink(peer, msg):
            print("[RECV] Cat007DownlinkRecord")

        @tx.on(Cat021Record)
        def on_cat021(peer, msg):
            print(f"[RECV] {Cat021Record.TYPE_NAME}")

        @tx.on(Cat048Record)
        def on_cat048(peer, msg):
            print(f"[RECV] {Cat048Record.TYPE_NAME}")

        @tx.on(Cat253Record)
        def on_cat253(peer, msg):
            print(f"[RECV] {Cat253Record.TYPE_NAME}")

        # ── State change callback (mirrors C++ tx.on_state_change()) ─

        tx.on_state_change(lambda peer, state:
            print(f"[STATE] peer={peer} -> {_state_name(state)}"))

        # ── Error callback (mirrors C++ tx.on_error()) ───────────────

        tx.on_error(lambda peer, peer_name, code, msg:
            print(f"[ERROR] peer={peer_name} code={code} {msg}",
                  file=sys.stderr))

        # ── Start ────────────────────────────────────────────────────

        tx.start()
        print("[poc_app] Started. Press Ctrl+C to stop.")

        # ── Send loop (mirrors C++ main loop) ────────────────────────

        rng = random.Random()
        interval_s = args.interval_ms / 1000.0

        while running:
            time.sleep(interval_s)
            if not running:
                break

            try:
                choice = rng.randint(0, 3)
                if choice == 0:
                    msg = random_asterix.random_cat007_uplink(rng)
                    print(f"[SEND] {Cat007UplinkRecord.TYPE_NAME}")
                    tx.send(peer_id, msg)
                elif choice == 1:
                    msg = random_asterix.random_cat021(rng)
                    print(f"[SEND] {Cat021Record.TYPE_NAME}")
                    tx.send(peer_id, msg)
                elif choice == 2:
                    msg = random_asterix.random_cat048(rng)
                    print(f"[SEND] {Cat048Record.TYPE_NAME}")
                    tx.send(peer_id, msg)
                elif choice == 3:
                    msg = random_asterix.random_cat253(rng)
                    print(f"[SEND] {Cat253Record.TYPE_NAME}")
                    tx.send(peer_id, msg)
            except ConduitError as e:
                if e.code == -4:
                    print("[SEND] no peers connected", file=sys.stderr)
                elif e.code == -5:
                    print(f"[SEND BLOCKED] {e}", file=sys.stderr)
                elif e.code == -6:
                    print(f"[SEND REJECTED] {e}", file=sys.stderr)
                else:
                    print(f"[SEND ERROR] {e}", file=sys.stderr)
            except Exception as e:
                traceback.print_exc(file=sys.stderr)

        # ── Stop & stats (mirrors C++ tx.stop() + tx.stats().snapshot()) ──

        print("[poc_app] Stopping...")
        tx.stop()

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

    print("[poc_app] Done.")


if __name__ == "__main__":
    main()
