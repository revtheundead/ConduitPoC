#!/usr/bin/env python3
"""Dummy ASTERIX Peer — TCP Server or Client mode (Python)

Server mode: listens for connections, sends Cat007Downlink/Cat021/Cat048/Cat253
             Uses asterix_alt module (server perspective: Downlink=send, Uplink=receive)
Client mode: identical to poc_app (sends uplink types)
             Uses asterix module (client perspective: Downlink=receive, Uplink=send)

Usage:
  python -m src.dummy_peer server [--port N] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]
  python -m src.dummy_peer client [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]
"""

import argparse
import random
import signal
import sys
import time

from conduit.transceiver import (
    Transceiver,
    TransceiverConfig,
    MessageLogMode,
    MessageLogOutput,
)
from conduit.transceiver.transport import TcpClientConfig, TcpServerConfig
from conduit.generated import asterix, asterix_alt
from conduit import ErrorCode

from . import random_asterix, random_asterix_alt

running = True


def signal_handler(sig, frame):
    global running
    running = False


# ── Handlers ────────────────────────────────────────────────────────────────

def register_server_handlers(tx: Transceiver) -> None:
    @tx.on(asterix_alt.Cat007UplinkRecord)
    def on_cat007_uplink(msg):
        print("[RECV] Cat007UplinkRecord")

    @tx.on(asterix_alt.Cat021Record)
    def on_cat021(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    @tx.on(asterix_alt.Cat048Record)
    def on_cat048(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    @tx.on(asterix_alt.Cat253Record)
    def on_cat253(msg):
        print(f"[RECV] {msg.TYPE_NAME}")


def register_client_handlers(tx: Transceiver) -> None:
    @tx.on(asterix.Cat007DownlinkRecord)
    def on_cat007_downlink(msg):
        print("[RECV] Cat007DownlinkRecord")

    @tx.on(asterix.Cat021Record)
    def on_cat021(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    @tx.on(asterix.Cat048Record)
    def on_cat048(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    @tx.on(asterix.Cat253Record)
    def on_cat253(msg):
        print(f"[RECV] {msg.TYPE_NAME}")


# ── Send helpers ────────────────────────────────────────────────────────────

def send_server_message(tx: Transceiver, rng: random.Random) -> None:
    choice = rng.randint(0, 3)
    if choice == 0:
        msg = random_asterix_alt.random_cat007_downlink(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    elif choice == 1:
        msg = random_asterix_alt.random_cat021(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    elif choice == 2:
        msg = random_asterix_alt.random_cat048(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    else:
        msg = random_asterix_alt.random_cat253(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    _report_send_result(result)


def send_client_message(tx: Transceiver, rng: random.Random) -> None:
    choice = rng.randint(0, 3)
    if choice == 0:
        msg = random_asterix.random_cat007_uplink(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    elif choice == 1:
        msg = random_asterix.random_cat021(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    elif choice == 2:
        msg = random_asterix.random_cat048(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    else:
        msg = random_asterix.random_cat253(rng)
        print(f"[SEND] {msg.TYPE_NAME}")
        result = tx.send(msg)
    _report_send_result(result)


def _report_send_result(result) -> None:
    if not result.is_ok():
        code = result.error.code
        if code == ErrorCode.DIRECTION_VIOLATION:
            print(f"[SEND BLOCKED] {result.error.format_short()}", file=sys.stderr)
        elif code == ErrorCode.ENCODE_CONSTRAINT_VIOLATION:
            print(f"[SEND REJECTED] {result.error.format_short()}", file=sys.stderr)
        else:
            print(f"[SEND ERROR] {result.error.format_short()}", file=sys.stderr)


# ── Stats ───────────────────────────────────────────────────────────────────

def print_stats(tx: Transceiver) -> None:
    s = tx.stats().snapshot()
    print(f"[STATS] received={s.messages_received}\n"
          f" dispatched={s.messages_dispatched}\n"
          f" dropped={s.messages_dropped}\n"
          f" decode_errors={s.decode_errors}\n"
          f" handler_errors={s.handler_errors}\n"
          f" handler_timeouts={s.handler_timeouts}\n"
          f" bytes_rx={s.bytes_received}\n"
          f" bytes_tx={s.bytes_sent}\n")


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    global running

    parser = argparse.ArgumentParser(description="Dummy ASTERIX Peer")
    subparsers = parser.add_subparsers(dest="mode", required=True)

    # Server subcommand
    server_p = subparsers.add_parser("server")
    server_p.add_argument("--port", type=int, default=5000)
    server_p.add_argument("--interval-ms", type=int, default=1000)
    server_p.add_argument("--log-dir", default="./logs")
    server_p.add_argument("--log-prefix", default="server")

    # Client subcommand
    client_p = subparsers.add_parser("client")
    client_p.add_argument("host", nargs="?", default="127.0.0.1")
    client_p.add_argument("port", nargs="?", type=int, default=5000)
    client_p.add_argument("--interval-ms", type=int, default=1000)
    client_p.add_argument("--log-dir", default="./logs")
    client_p.add_argument("--log-prefix", default="client")

    args = parser.parse_args()

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    if args.mode == "server":
        _run_server(args)
    else:
        _run_client(args)

    print("[dummy_peer] Done.")


def _run_server(args) -> None:
    global running

    print(f"[dummy_peer] Server mode on port {args.port} "
          f"(interval={args.interval_ms}ms)")

    cfg = TransceiverConfig()
    cfg.message_log.enabled = True
    cfg.message_log.mode = MessageLogMode.SEPARATE_DIRECTION
    cfg.message_log.output = MessageLogOutput.FILE
    cfg.message_log.directory = args.log_dir
    cfg.message_log.prefix = args.log_prefix
    cfg.add_peer("clients",
                 asterix_alt.create_asterix_data_block_session,
                 TcpServerConfig(bind_address="0.0.0.0", port=args.port))

    tx = Transceiver(cfg)
    register_server_handlers(tx)

    @tx.on_state_change
    def on_state(peer_id, state):
        print(f"[STATE] peer={peer_id.value} -> {state}")

    @tx.on_error
    def on_error(event):
        print(f"[ERROR] peer={event.peer_name} {event.error.format_short()}",
              file=sys.stderr)

    result = tx.start()
    if not result.is_ok():
        print(f"[ERROR] Failed to start: {result.error.format_short()}",
              file=sys.stderr)
        sys.exit(1)

    print("[dummy_peer] Listening. Press Ctrl+C to stop.")

    rng = random.Random()
    interval_s = args.interval_ms / 1000.0
    while running:
        time.sleep(interval_s)
        if not running:
            break
        send_server_message(tx, rng)

    print("[dummy_peer] Stopping...")
    tx.stop()
    print_stats(tx)


def _run_client(args) -> None:
    global running

    print(f"[dummy_peer] Client mode connecting to {args.host}:{args.port} "
          f"(interval={args.interval_ms}ms)")

    cfg = TransceiverConfig()
    cfg.message_log.enabled = True
    cfg.message_log.mode = MessageLogMode.SEPARATE_DIRECTION
    cfg.message_log.output = MessageLogOutput.FILE
    cfg.message_log.directory = args.log_dir
    cfg.message_log.prefix = args.log_prefix
    cfg.add_peer("server",
                 asterix.create_asterix_data_block_session,
                 TcpClientConfig(host=args.host, port=args.port))

    tx = Transceiver(cfg)
    register_client_handlers(tx)

    @tx.on_state_change
    def on_state(peer_id, state):
        print(f"[STATE] peer={peer_id.value} -> {state}")

    @tx.on_error
    def on_error(event):
        print(f"[ERROR] peer={event.peer_name} {event.error.format_short()}",
              file=sys.stderr)

    result = tx.start()
    if not result.is_ok():
        print(f"[ERROR] Failed to start: {result.error.format_short()}",
              file=sys.stderr)
        sys.exit(1)

    print("[dummy_peer] Started. Press Ctrl+C to stop.")

    rng = random.Random()
    interval_s = args.interval_ms / 1000.0
    while running:
        time.sleep(interval_s)
        if not running:
            break
        send_client_message(tx, rng)

    print("[dummy_peer] Stopping...")
    tx.stop()
    print_stats(tx)


if __name__ == "__main__":
    main()
