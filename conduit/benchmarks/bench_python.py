#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Conduit - Python codec benchmark
#
# Mirrors the C++ benchmark suite (bench_codec, bench_stress_codec,
# bench_throughput) using the Python ctypes bindings through the C ABI.
#
# Usage:
#   python3 benchmarks/bench_python.py
#
# Environment:
#   CONDUIT_CODEC_LIB  Path to libconduit_codec_cabi_bench.so

from __future__ import annotations

import os
import sys
import time
import random

# ---------------------------------------------------------------------------
# Setup paths
# ---------------------------------------------------------------------------

_BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_BENCH_DIR, ".."))
_BUILD_DIR = os.path.join(_PROJECT_ROOT, "build")

# Point to benchmark CABI library
_lib_path = os.environ.get("CONDUIT_CODEC_LIB", "")
if not _lib_path or not os.path.isfile(_lib_path):
    for name in ["libconduit_codec_cabi_bench.so", "libconduit_codec_cabi_bench.dylib",
                  "conduit_codec_cabi_bench.dll"]:
        candidate = os.path.join(_PROJECT_ROOT, "lib", name)
        if os.path.isfile(candidate):
            _lib_path = candidate
            break
    if not _lib_path or not os.path.isfile(_lib_path):
        print("ERROR: Cannot find libconduit_codec_cabi_bench.", file=sys.stderr)
        print("Build with: cmake -DCONDUIT_BUILD_PYTHON_BENCHMARKS=ON", file=sys.stderr)
        sys.exit(1)
os.environ["CONDUIT_CODEC_LIB"] = _lib_path

# Add Python bindings to path
_bindings_dir = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _bindings_dir not in sys.path:
    sys.path.insert(0, _bindings_dir)

# Add generated Python code to path
_gen_dir = os.path.join(_BUILD_DIR, "benchmarks", "generated")
for sub in ["stress_py", "sentry_link_py"]:
    p = os.path.join(_gen_dir, sub)
    if p not in sys.path:
        sys.path.insert(0, p)

# Force fresh library load
import conduit.codec_binding as _codec_mod
_codec_mod._lib = None

from conduit.codec_binding import CodecSession

# Import generated Python message types
from stress.messages import (
    Ping, TelemetryReport, SurveillanceRecord, SurveillanceRecordItems,
    StressFrame,
)
from stress.structs import (
    Coordinate, Velocity, GeoPosition, TrackQuality, ExtendedStatus,
    PeriodicPayload, EventPayload, ReadingsItem, ReadingsItemEntries,
)
from stress.types import (
    PriorityLevel, SensorType, ReportClass,
    Temperature, PressureHpa, Wgs84, AltitudeFl, AngleDeg, StatusFlags,
)
from stress.constants import Constants as StressConstants

from sentry_link.messages import (
    HeartbeatBody, AlertBody, SensorBody, ConfigBody, Frame as SentryFrame,
)
from sentry_link.structs import SensorFlags, FirmwareVersion
from sentry_link.types import DeviceStatus, SeverityLevel, DeviceMode

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

STRESS_BATCH = 100000
BATCH_SIZE = 10000
WARMUP_ITERS = 100
BENCH_SAMPLES = 10


# ---------------------------------------------------------------------------
# Message factories (match C++ bench_codec.cpp and bench_stress_codec.cpp)
# ---------------------------------------------------------------------------

def make_heartbeat() -> HeartbeatBody:
    hb = HeartbeatBody()
    hb.timestamp = 1700000000
    hb.uptime_hours = 720
    hb.status = DeviceStatus.ONLINE
    hb.cpu_load = 65
    return hb


def make_alert() -> AlertBody:
    ab = AlertBody()
    ab.timestamp = 1700000100
    ab.source_id = 42
    ab.severity = SeverityLevel.WARNING
    ab.category = 5
    ab.alert_code = 1001
    ab.message = "Sensor temperature exceeds limit"
    return ab


def make_sensor() -> SensorBody:
    sb = SensorBody()
    sb.sensor_id = 101
    sb.timestamp = 1700000200
    flags = SensorFlags()
    flags.channel = 3
    flags.precision = 2
    flags.saturated = 0
    flags.valid = 1
    sb.flags = flags
    sb.raw_value = 23.45
    sb.unit_code = 1
    return sb


def make_config() -> ConfigBody:
    cb = ConfigBody()
    cb.device_name = "sensor-node-01"
    fw = FirmwareVersion()
    fw.major = 2
    fw.minor = 5
    fw.patch = 1024
    cb.firmware = fw
    cb.mode = DeviceMode.ACTIVE
    cb.log_level = 3
    cb.auto_report = 1
    cb.compression = 0
    cb.sample_rate = 1000
    return cb


def make_ping() -> Ping:
    p = Ping()
    p.sequence = 12345
    p.timestamp = 1700000000
    p.priority = PriorityLevel.HIGH
    f = StatusFlags()
    f.active = True
    f.calibrated = True
    f.gps_lock = True
    p.flags = f
    p.tag = 0xBEEF
    return p


def make_telemetry() -> TelemetryReport:
    tr = TelemetryReport()
    tr.sequence = 67890
    tr.timestamp = 1700001000
    tr.source_id = 42
    tr.sensor = SensorType.ACCELERATION
    pos = Coordinate()
    pos.x = 100000
    pos.y = -200000
    pos.z = 5000
    tr.position = pos
    vel = Velocity()
    vel.vx = 150
    vel.vy = -75
    vel.vz = 10
    tr.velocity = vel
    temp = Temperature()
    temp.value = 23.5
    tr.temperature = temp
    pres = PressureHpa()
    pres.value = 1013.25
    tr.pressure = pres
    hdg = AngleDeg()
    hdg.value = 270.0
    tr.heading = hdg
    st = StatusFlags()
    st.active = True
    st.recording = True
    tr.status = st
    tr.label = "SENSOR-ALPHA"
    return tr


def make_surveillance_typical() -> SurveillanceRecord:
    sr = SurveillanceRecord()
    sr.track_id = 1001
    sr.timestamp = 1700002000
    sr.report_class = ReportClass.PERIODIC
    sr.items.source_id = 42
    gp = GeoPosition()
    lat = Wgs84()
    lat.value = 41.015137
    lon = Wgs84()
    lon.value = 28.979530
    alt = AltitudeFl()
    alt.value = 350.0
    gp.latitude = lat
    gp.longitude = lon
    gp.altitude = alt
    sr.items.geo_position = gp
    cart = Coordinate()
    cart.x = 50000
    cart.y = -30000
    cart.z = 1400
    sr.items.cartesian = cart
    vel = Velocity()
    vel.vx = 200
    vel.vy = -100
    vel.vz = 5
    sr.items.velocity = vel
    hdg = AngleDeg()
    hdg.value = 135.0
    sr.items.heading = hdg
    tq = TrackQuality()
    tq.confidence = 12
    tq.freshness = 8
    tq.source = 3
    sr.items.quality = tq
    es = ExtendedStatus()
    es.mode = 5
    es.reliability = 10
    es.secondary_mode = 2
    es.aux_flags = 7
    sr.items.status = es
    sr.items.label = "TRK1001"
    pp = PeriodicPayload()
    pp.interval_ms = 1000
    pp.counter = 9999
    sr.payload = pp
    return sr


def make_surveillance_maximal() -> SurveillanceRecord:
    sr = SurveillanceRecord()
    sr.track_id = 2002
    sr.timestamp = 1700003000
    sr.report_class = ReportClass.EVENT_DRIVEN
    sr.items.source_id = 99
    gp = GeoPosition()
    lat = Wgs84()
    lat.value = 51.5074
    lon = Wgs84()
    lon.value = -0.1278
    alt = AltitudeFl()
    alt.value = 120.0
    gp.latitude = lat
    gp.longitude = lon
    gp.altitude = alt
    sr.items.geo_position = gp
    cart = Coordinate()
    cart.x = -75000
    cart.y = 120000
    cart.z = -500
    sr.items.cartesian = cart
    vel = Velocity()
    vel.vx = -300
    vel.vy = 250
    vel.vz = -20
    sr.items.velocity = vel
    hdg = AngleDeg()
    hdg.value = 45.0
    sr.items.heading = hdg
    ar = AltitudeFl()
    ar.value = -12.5
    sr.items.altitude_rate = ar
    tq = TrackQuality()
    tq.confidence = 15
    tq.freshness = 14
    tq.source = 7
    sr.items.quality = tq
    es = ExtendedStatus()
    es.mode = 7
    es.reliability = 15
    es.secondary_mode = 5
    es.aux_flags = 12
    sr.items.status = es
    sr.items.label = "MAXREC"
    temp = Temperature()
    temp.value = -15.5
    sr.items.temperature = temp
    pres = PressureHpa()
    pres.value = 250.0
    sr.items.pressure = pres
    sr.items.sensor = SensorType.MAGNETIC
    ri = ReadingsItem()
    ri.rep = 3
    entries = []
    for ch in range(3):
        e = ReadingsItemEntries()
        e.channel = ch
        e.value = 1000 + ch * 100
        e.quality = 90 + ch
        entries.append(e)
    ri.entries = entries
    sr.items.readings = ri
    sr.items.raw_data = bytes([0xA0 + i for i in range(16)])
    ep = EventPayload()
    ep.event_code = 5001
    ep.severity = PriorityLevel.CRITICAL
    ep.description = "Surveillance boundary alert"
    sr.payload = ep
    return sr


def encode_sentry(msg: object) -> bytes:
    frame = SentryFrame.wrap(msg)
    return frame.encode_bytes()


def encode_stress(msg: object) -> bytes:
    frame = StressFrame.wrap(msg)
    return frame.encode_bytes()


# ---------------------------------------------------------------------------
# Benchmark helper
# ---------------------------------------------------------------------------

def bench(name: str, fn, iterations: int = 0, samples: int = BENCH_SAMPLES) -> float:
    """Run fn repeatedly and report timing.

    If iterations > 0, fn is expected to run that many iterations internally.
    Returns median ns per call.
    """
    # Warmup
    for _ in range(min(WARMUP_ITERS, 10)):
        fn()

    times = []
    for _ in range(samples):
        t0 = time.perf_counter_ns()
        fn()
        t1 = time.perf_counter_ns()
        times.append(t1 - t0)

    times.sort()
    median_ns = times[len(times) // 2]

    if iterations > 0:
        per_iter_ns = median_ns / iterations
        total_ms = median_ns / 1e6
        rate = iterations / (median_ns / 1e9) if median_ns > 0 else 0
        print(f"  {name:<45s} {total_ms:>10.1f} ms  ({per_iter_ns:>8.0f} ns/msg, {rate:>10.0f} msgs/s)")
        return per_iter_ns
    else:
        ns = median_ns
        print(f"  {name:<45s} {ns:>10.0f} ns")
        return ns


# ---------------------------------------------------------------------------
# Codec: encode
# ---------------------------------------------------------------------------

def run_codec_encode(msgs: dict) -> None:
    print("\nCodec: encode (Python generated codec)")
    print("-------------------------------------------")

    for label, fn in [
        ("encode sentry Heartbeat", lambda: encode_sentry(msgs["heartbeat"])),
        ("encode sentry Alert", lambda: encode_sentry(msgs["alert"])),
        ("encode sentry Sensor", lambda: encode_sentry(msgs["sensor"])),
        ("encode sentry Config", lambda: encode_sentry(msgs["config"])),
        ("encode stress Ping", lambda: encode_stress(msgs["ping"])),
        ("encode stress Telemetry", lambda: encode_stress(msgs["telemetry"])),
        ("encode stress Surveillance typical", lambda: encode_stress(msgs["surv_typ"])),
        ("encode stress Surveillance maximal", lambda: encode_stress(msgs["surv_max"])),
    ]:
        bench(label, fn)


# ---------------------------------------------------------------------------
# Codec: decode
# ---------------------------------------------------------------------------

def run_codec_decode(session_stress: CodecSession, session_sentry: CodecSession,
                     wire: dict) -> None:
    print("\nCodec: decode (Python ctypes → C ABI)")
    print("-------------------------------------------")

    for label, (session, data) in [
        ("decode sentry Heartbeat", (session_sentry, wire["heartbeat"])),
        ("decode sentry Alert", (session_sentry, wire["alert"])),
        ("decode sentry Sensor", (session_sentry, wire["sensor"])),
        ("decode sentry Config", (session_sentry, wire["config"])),
        ("decode stress Ping", (session_stress, wire["ping"])),
        ("decode stress Telemetry", (session_stress, wire["telemetry"])),
        ("decode stress Surveillance typical", (session_stress, wire["surv_typ"])),
        ("decode stress Surveillance maximal", (session_stress, wire["surv_max"])),
    ]:
        bench(label, lambda s=session, d=data: s.decode_frame(d))


# ---------------------------------------------------------------------------
# Codec: roundtrip
# ---------------------------------------------------------------------------

def run_codec_roundtrip(session_stress: CodecSession, session_sentry: CodecSession,
                        msgs: dict) -> None:
    print("\nCodec: roundtrip (Python encode → C ABI decode)")
    print("-------------------------------------------")

    for label, (session, msg, enc_fn) in [
        ("roundtrip sentry Heartbeat", (session_sentry, msgs["heartbeat"], encode_sentry)),
        ("roundtrip sentry Alert", (session_sentry, msgs["alert"], encode_sentry)),
        ("roundtrip stress Ping", (session_stress, msgs["ping"], encode_stress)),
        ("roundtrip stress Telemetry", (session_stress, msgs["telemetry"], encode_stress)),
        ("roundtrip stress Surveillance typical", (session_stress, msgs["surv_typ"], encode_stress)),
        ("roundtrip stress Surveillance maximal", (session_stress, msgs["surv_max"], encode_stress)),
    ]:
        def roundtrip(s=session, m=msg, e=enc_fn):
            w = e(m)
            return s.decode_frame(w)
        bench(label, roundtrip)


# ---------------------------------------------------------------------------
# Stress: encode/decode/roundtrip 100K
# ---------------------------------------------------------------------------

def run_stress(session_stress: CodecSession, msgs: dict, wire: dict) -> None:
    print("\nStress: encode 100K (Python generated codec)")
    print("-------------------------------------------")

    for label, (msg, enc_fn) in [
        ("100K Ping encode", (msgs["ping"], encode_stress)),
        ("100K TelemetryReport encode", (msgs["telemetry"], encode_stress)),
        ("100K Surveillance typical encode", (msgs["surv_typ"], encode_stress)),
        ("100K Surveillance maximal encode", (msgs["surv_max"], encode_stress)),
    ]:
        def do_encode(m=msg, e=enc_fn):
            for _ in range(STRESS_BATCH):
                e(m)
        bench(label, do_encode, iterations=STRESS_BATCH, samples=3)

    print("\nStress: decode 100K (Python ctypes → C ABI)")
    print("-------------------------------------------")

    for label, data in [
        ("100K Ping decode", wire["ping"]),
        ("100K TelemetryReport decode", wire["telemetry"]),
        ("100K Surveillance typical decode", wire["surv_typ"]),
        ("100K Surveillance maximal decode", wire["surv_max"]),
    ]:
        def do_decode(d=data):
            for _ in range(STRESS_BATCH):
                session_stress.decode_frame(d)
        bench(label, do_decode, iterations=STRESS_BATCH, samples=3)

    print("\nStress: roundtrip 100K (Python encode → C ABI decode)")
    print("-------------------------------------------")

    for label, (msg, enc_fn) in [
        ("100K Ping roundtrip", (msgs["ping"], encode_stress)),
        ("100K TelemetryReport roundtrip", (msgs["telemetry"], encode_stress)),
        ("100K Surveillance typical roundtrip", (msgs["surv_typ"], encode_stress)),
        ("100K Surveillance maximal roundtrip", (msgs["surv_max"], encode_stress)),
    ]:
        def do_roundtrip(m=msg, e=enc_fn):
            for _ in range(STRESS_BATCH):
                w = e(m)
                session_stress.decode_frame(w)
        bench(label, do_roundtrip, iterations=STRESS_BATCH, samples=3)


# ---------------------------------------------------------------------------
# Stress: mixed workload
# ---------------------------------------------------------------------------

def run_mixed(session_stress: CodecSession, wire: dict) -> None:
    print("\nStress: mixed workload 100K (Python ctypes → C ABI)")
    print("-------------------------------------------")

    rng = random.Random(42)
    # 40% Ping, 35% Telemetry, 25% Surveillance
    weights = [0.40, 0.75, 1.0]  # cumulative
    type_indices = []
    for _ in range(STRESS_BATCH):
        r = rng.random()
        if r < weights[0]:
            type_indices.append(0)
        elif r < weights[1]:
            type_indices.append(1)
        else:
            type_indices.append(2)

    frames = [wire["ping"], wire["telemetry"], wire["surv_typ"]]

    def do_mixed():
        for i in range(STRESS_BATCH):
            session_stress.decode_frame(frames[type_indices[i]])

    bench("100K mixed decode", do_mixed, iterations=STRESS_BATCH, samples=3)


# ---------------------------------------------------------------------------
# Throughput: batch encode/decode 10K
# ---------------------------------------------------------------------------

def run_throughput(session_stress: CodecSession, session_sentry: CodecSession,
                   msgs: dict, wire: dict) -> None:
    print("\nThroughput: batch encode 10K (Python generated codec)")
    print("-------------------------------------------")

    for label, (msg, enc_fn) in [
        ("10000x sentry Heartbeat encode", (msgs["heartbeat"], encode_sentry)),
        ("10000x sentry Alert encode", (msgs["alert"], encode_sentry)),
        ("10000x stress Ping encode", (msgs["ping"], encode_stress)),
        ("10000x stress Telemetry encode", (msgs["telemetry"], encode_stress)),
    ]:
        def do_batch_encode(m=msg, e=enc_fn):
            for _ in range(BATCH_SIZE):
                e(m)
        bench(label, do_batch_encode, iterations=BATCH_SIZE, samples=5)

    print("\nThroughput: batch decode 10K (Python ctypes → C ABI)")
    print("-------------------------------------------")

    for label, (session, data) in [
        ("10000x sentry Heartbeat decode", (session_sentry, wire["heartbeat"])),
        ("10000x sentry Alert decode", (session_sentry, wire["alert"])),
        ("10000x stress Ping decode", (session_stress, wire["ping"])),
        ("10000x stress Telemetry decode", (session_stress, wire["telemetry"])),
    ]:
        def do_batch_decode(s=session, d=data):
            for _ in range(BATCH_SIZE):
                s.decode_frame(d)
        bench(label, do_batch_decode, iterations=BATCH_SIZE, samples=5)


# ===========================================================================
# Main
# ===========================================================================

def main() -> None:
    print("=" * 60)
    print("Conduit Python Benchmark (ctypes → C ABI)")
    print("=" * 60)

    # Create sessions
    session_stress = CodecSession("stress_frame")
    session_sentry = CodecSession("sentry_link")

    # Pre-built message objects for encode benchmarks
    msgs = {
        "heartbeat": make_heartbeat(),
        "alert": make_alert(),
        "sensor": make_sensor(),
        "config": make_config(),
        "ping": make_ping(),
        "telemetry": make_telemetry(),
        "surv_typ": make_surveillance_typical(),
        "surv_max": make_surveillance_maximal(),
    }

    # Build wire bytes using generated Python code
    wire = {
        "heartbeat": encode_sentry(msgs["heartbeat"]),
        "alert": encode_sentry(msgs["alert"]),
        "sensor": encode_sentry(msgs["sensor"]),
        "config": encode_sentry(msgs["config"]),
        "ping": encode_stress(msgs["ping"]),
        "telemetry": encode_stress(msgs["telemetry"]),
        "surv_typ": encode_stress(msgs["surv_typ"]),
        "surv_max": encode_stress(msgs["surv_max"]),
    }

    print(f"\nLibrary: {_lib_path}")
    print(f"Sessions: stress_frame ({session_stress.protocol_name()}), "
          f"sentry_link ({session_sentry.protocol_name()})")
    print(f"Wire sizes: " + ", ".join(f"{k}={len(v)}B" for k, v in wire.items()))

    # Run benchmarks
    run_codec_encode(msgs)
    run_codec_decode(session_stress, session_sentry, wire)
    run_codec_roundtrip(session_stress, session_sentry, msgs)
    run_stress(session_stress, msgs, wire)
    run_mixed(session_stress, wire)
    run_throughput(session_stress, session_sentry, msgs, wire)

    # Cleanup
    session_stress.close()
    session_sentry.close()

    print("\n" + "=" * 60)
    print("Python benchmark complete.")
    print("=" * 60)


if __name__ == "__main__":
    main()
