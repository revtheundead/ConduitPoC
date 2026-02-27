#!/usr/bin/env python3
"""PoC ASTERIX Transceiver Application — TCP Client (Python)

Connects to a peer, sends random Cat007Uplink/Cat021/Cat048/Cat253 messages,
and logs all received messages to stdout.

Usage: python -m src.poc_app [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX]
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
from conduit.transceiver.transport import TcpClientConfig
from conduit.generated.asterix import (
    create_asterix_data_block_session,
    Cat007DownlinkRecord,
    Cat021Record,
    Cat048Record,
    Cat253Record,
)
from conduit import ErrorCode

from . import random_asterix

running = True


def signal_handler(sig, frame):
    global running
    running = False


def main():
    global running

    parser = argparse.ArgumentParser(description="PoC ASTERIX TCP Client")
    parser.add_argument("host", nargs="?", default="127.0.0.1")
    parser.add_argument("port", nargs="?", type=int, default=5000)
    parser.add_argument("--interval-ms", type=int, default=1000)
    parser.add_argument("--log-dir", default="./logs")
    parser.add_argument("--log-prefix", default="poc")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    print(f"[poc_app] Connecting to {args.host}:{args.port} "
          f"(interval={args.interval_ms}ms)")

    # Build transceiver config
    cfg = TransceiverConfig()
    cfg.message_log.enabled = True
    cfg.message_log.mode = MessageLogMode.SEPARATE_DIRECTION
    cfg.message_log.output = MessageLogOutput.FILE
    cfg.message_log.directory = args.log_dir
    cfg.message_log.prefix = args.log_prefix
    cfg.add_peer("server",
                 create_asterix_data_block_session,
                 TcpClientConfig(host=args.host, port=args.port))

    tx = Transceiver(cfg)

    # Register typed receive handlers
    @tx.on(Cat007DownlinkRecord)
    def on_cat007_downlink(msg):
        print("[RECV] Cat007DownlinkRecord")

    @tx.on(Cat021Record)
    def on_cat021(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    @tx.on(Cat048Record)
    def on_cat048(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    @tx.on(Cat253Record)
    def on_cat253(msg):
        print(f"[RECV] {msg.TYPE_NAME}")

    # Connection state logging
    @tx.on_state_change
    def on_state(peer_id, state):
        print(f"[STATE] peer={peer_id.value} -> {state}")

    # Structured error reporting
    @tx.on_error
    def on_error(event):
        print(f"[ERROR] peer={event.peer_name} {event.error.format_short()}",
              file=sys.stderr)

    # Start
    result = tx.start()
    if not result.is_ok():
        print(f"[ERROR] Failed to start: {result.error.format_short()}",
              file=sys.stderr)
        sys.exit(1)

    print("[poc_app] Started. Press Ctrl+C to stop.")

    # Send loop
    rng = random.Random()
    interval_s = args.interval_ms / 1000.0

    while running:
        time.sleep(interval_s)
        if not running:
            break

        choice = rng.randint(0, 3)
        if choice == 0:
            msg = random_asterix.random_cat007_uplink(rng)
            print(f"[SEND] {msg.TYPE_NAME}")
            send_result = tx.send(msg)
        elif choice == 1:
            msg = random_asterix.random_cat021(rng)
            print(f"[SEND] {msg.TYPE_NAME}")
            send_result = tx.send(msg)
        elif choice == 2:
            msg = random_asterix.random_cat048(rng)
            print(f"[SEND] {msg.TYPE_NAME}")
            send_result = tx.send(msg)
        else:
            msg = random_asterix.random_cat253(rng)
            print(f"[SEND] {msg.TYPE_NAME}")
            send_result = tx.send(msg)

        if not send_result.is_ok():
            code = send_result.error.code
            if code == ErrorCode.DIRECTION_VIOLATION:
                print(f"[SEND BLOCKED] {send_result.error.format_short()}",
                      file=sys.stderr)
            elif code == ErrorCode.ENCODE_CONSTRAINT_VIOLATION:
                print(f"[SEND REJECTED] {send_result.error.format_short()}",
                      file=sys.stderr)
            else:
                print(f"[SEND ERROR] {send_result.error.format_short()}",
                      file=sys.stderr)

    print("[poc_app] Stopping...")
    tx.stop()

    s = tx.stats().snapshot()
    print(f"[STATS] received={s.messages_received}\n"
          f" dispatched={s.messages_dispatched}\n"
          f" dropped={s.messages_dropped}\n"
          f" decode_errors={s.decode_errors}\n"
          f" handler_errors={s.handler_errors}\n"
          f" handler_timeouts={s.handler_timeouts}\n"
          f" bytes_rx={s.bytes_received}\n"
          f" bytes_tx={s.bytes_sent}\n")

    print("[poc_app] Done.")


if __name__ == "__main__":
    main()
