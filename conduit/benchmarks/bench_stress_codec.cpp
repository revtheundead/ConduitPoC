// SPDX-License-Identifier: MIT
// Conduit - Stress codec benchmarks
//
// Pushes 100K messages through the codec to find real encode/decode limits.
// Three tiers of message complexity: simple (Ping), medium (TelemetryReport),
// complex (SurveillanceRecord with bitmap, FX chain, choice, arrays).

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include <stress/messages.hpp>
#include <stress/sessions.hpp>

#include <random>
#include <vector>

static constexpr size_t STRESS_BATCH = 100000;

// ============================================================================
// Message factories
// ============================================================================

namespace {

stress::Ping make_ping() {
    stress::Ping p;
    p.set_sequence(12345);
    p.set_timestamp(1700000000);
    p.set_priority(stress::priority_level::high);
    stress::status_flags f;
    f.set_active(true);
    f.set_calibrated(true);
    f.set_gps_lock(true);
    p.set_flags(f);
    p.set_tag(0xBEEF);
    return p;
}

stress::TelemetryReport make_telemetry() {
    stress::TelemetryReport tr;
    tr.set_sequence(67890);
    tr.set_timestamp(1700001000);
    tr.set_source_id(42);
    tr.set_sensor(stress::sensor_type::acceleration);

    stress::Coordinate pos;
    pos.set_x(100000);
    pos.set_y(-200000);
    pos.set_z(5000);
    tr.set_position(pos);

    stress::Velocity vel;
    vel.set_vx(150);
    vel.set_vy(-75);
    vel.set_vz(10);
    tr.set_velocity(vel);

    stress::temperature temp;
    temp.set_value(23.5);
    tr.set_temperature(temp);

    stress::pressure_hpa pres;
    pres.set_value(1013.25);
    tr.set_pressure(pres);

    stress::angle_deg hdg;
    hdg.set_value(270.0);
    tr.set_heading(hdg);

    stress::status_flags st;
    st.set_active(true);
    st.set_recording(true);
    tr.set_status(st);

    tr.set_label("SENSOR-ALPHA");
    return tr;
}

stress::SurveillanceRecord make_surveillance_typical() {
    stress::SurveillanceRecord sr;
    sr.set_track_id(1001);
    sr.set_timestamp(1700002000);
    sr.set_report_class(stress::report_class::periodic);

    auto& it = sr.mutable_items();

    it.set_source_id(42);

    stress::GeoPosition gp;
    stress::wgs84 lat; lat.set_value(41.015137);
    stress::wgs84 lon; lon.set_value(28.979530);
    stress::altitude_fl alt; alt.set_value(350.0);
    gp.set_latitude(lat);
    gp.set_longitude(lon);
    gp.set_altitude(alt);
    it.set_geo_position(gp);

    stress::Coordinate cart;
    cart.set_x(50000);
    cart.set_y(-30000);
    cart.set_z(1400);
    it.set_cartesian(cart);

    stress::Velocity vel;
    vel.set_vx(200);
    vel.set_vy(-100);
    vel.set_vz(5);
    it.set_velocity(vel);

    stress::angle_deg hdg;
    hdg.set_value(135.0);
    it.set_heading(hdg);

    stress::TrackQuality tq;
    tq.set_confidence(12);
    tq.set_freshness(8);
    tq.set_source(3);
    it.set_quality(tq);

    stress::ExtendedStatus es;
    es.set_mode(5);
    es.set_reliability(10);
    es.set_secondary_mode(2);
    es.set_aux_flags(7);
    it.set_status(es);

    it.set_label("TRK1001");

    // Periodic payload
    stress::PeriodicPayload pp;
    pp.set_interval_ms(1000);
    pp.set_counter(9999);
    sr.set_payload(pp);

    return sr;
}

stress::SurveillanceRecord make_surveillance_maximal() {
    stress::SurveillanceRecord sr;
    sr.set_track_id(2002);
    sr.set_timestamp(1700003000);
    sr.set_report_class(stress::report_class::event_driven);

    auto& it = sr.mutable_items();

    it.set_source_id(99);

    stress::GeoPosition gp;
    stress::wgs84 lat; lat.set_value(51.5074);
    stress::wgs84 lon; lon.set_value(-0.1278);
    stress::altitude_fl alt; alt.set_value(120.0);
    gp.set_latitude(lat);
    gp.set_longitude(lon);
    gp.set_altitude(alt);
    it.set_geo_position(gp);

    stress::Coordinate cart;
    cart.set_x(-75000);
    cart.set_y(120000);
    cart.set_z(-500);
    it.set_cartesian(cart);

    stress::Velocity vel;
    vel.set_vx(-300);
    vel.set_vy(250);
    vel.set_vz(-20);
    it.set_velocity(vel);

    stress::angle_deg hdg;
    hdg.set_value(45.0);
    it.set_heading(hdg);

    stress::altitude_fl ar;
    ar.set_value(-12.5);
    it.set_altitude_rate(ar);

    stress::TrackQuality tq;
    tq.set_confidence(15);
    tq.set_freshness(14);
    tq.set_source(7);
    it.set_quality(tq);

    stress::ExtendedStatus es;
    es.set_mode(7);
    es.set_reliability(15);
    es.set_secondary_mode(5);
    es.set_aux_flags(12);
    it.set_status(es);

    it.set_label("MAXREC");

    stress::temperature temp;
    temp.set_value(-15.5);
    it.set_temperature(temp);

    stress::pressure_hpa pres;
    pres.set_value(250.0);
    it.set_pressure(pres);

    it.set_sensor(stress::sensor_type::magnetic);

    // Readings with 3 elements
    stress::ReadingsItem ri;
    ri.set_rep(3);
    auto& entries = ri.mutable_entries();
    for (uint8_t ch = 0; ch < 3; ++ch) {
        stress::ReadingsItem_entriesElement e;
        e.set_channel(ch);
        e.set_value(static_cast<stress::int16>(1000 + ch * 100));
        e.set_quality(static_cast<stress::uint8>(90 + ch));
        entries.push_back(e);
    }
    it.set_readings(ri);

    std::array<uint8_t, 16> raw{};
    for (size_t i = 0; i < 16; ++i) raw[i] = static_cast<uint8_t>(0xA0 + i);
    it.set_raw_data(raw);

    // Event payload
    stress::EventPayload ep;
    ep.set_event_code(5001);
    ep.set_severity(stress::priority_level::critical);
    ep.set_description("Surveillance boundary alert");
    sr.set_payload(ep);

    return sr;
}

// Encode helper via frame
template<typename T>
std::vector<uint8_t> encode_stress(const T& msg) {
    return stress::StressFrame::wrap(msg).encode_bytes().value();
}

} // namespace

// ============================================================================
// Stress: encode (100K iterations)
// ============================================================================

TEST_CASE("Stress: encode", "[stress][codec][encode]") {
    auto ping  = make_ping();
    auto telem = make_telemetry();
    auto surv_typ = make_surveillance_typical();
    auto surv_max = make_surveillance_maximal();

    BENCHMARK_ADVANCED("100K Ping encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(ping);
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };

    BENCHMARK_ADVANCED("100K TelemetryReport encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(telem);
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };

    BENCHMARK_ADVANCED("100K Surveillance typical encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(surv_typ);
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };

    BENCHMARK_ADVANCED("100K Surveillance maximal encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(surv_max);
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };
}

// ============================================================================
// Stress: decode (100K iterations, pre-encoded)
// ============================================================================

TEST_CASE("Stress: decode", "[stress][codec][decode]") {
    auto ping_wire      = encode_stress(make_ping());
    auto telem_wire     = encode_stress(make_telemetry());
    auto surv_typ_wire  = encode_stress(make_surveillance_typical());
    auto surv_max_wire  = encode_stress(make_surveillance_maximal());

    BENCHMARK_ADVANCED("100K Ping decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto result = stress::StressFrame::decode_bytes(ping_wire);
                if (result) ++decoded;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("100K TelemetryReport decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto result = stress::StressFrame::decode_bytes(telem_wire);
                if (result) ++decoded;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("100K Surveillance typical decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto result = stress::StressFrame::decode_bytes(surv_typ_wire);
                if (result) ++decoded;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("100K Surveillance maximal decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto result = stress::StressFrame::decode_bytes(surv_max_wire);
                if (result) ++decoded;
            }
            return decoded;
        });
    };
}

// ============================================================================
// Stress: roundtrip (encode + decode, 100K iterations)
// ============================================================================

TEST_CASE("Stress: roundtrip", "[stress][codec][roundtrip]") {
    auto ping  = make_ping();
    auto telem = make_telemetry();
    auto surv_typ = make_surveillance_typical();
    auto surv_max = make_surveillance_maximal();

    BENCHMARK_ADVANCED("100K Ping roundtrip")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t ok = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(ping);
                auto result = stress::StressFrame::decode_bytes(bytes);
                if (result) ++ok;
            }
            return ok;
        });
    };

    BENCHMARK_ADVANCED("100K TelemetryReport roundtrip")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t ok = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(telem);
                auto result = stress::StressFrame::decode_bytes(bytes);
                if (result) ++ok;
            }
            return ok;
        });
    };

    BENCHMARK_ADVANCED("100K Surveillance typical roundtrip")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t ok = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(surv_typ);
                auto result = stress::StressFrame::decode_bytes(bytes);
                if (result) ++ok;
            }
            return ok;
        });
    };

    BENCHMARK_ADVANCED("100K Surveillance maximal roundtrip")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t ok = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                auto bytes = encode_stress(surv_max);
                auto result = stress::StressFrame::decode_bytes(bytes);
                if (result) ++ok;
            }
            return ok;
        });
    };
}

// ============================================================================
// Stress: mixed workload (40% Ping, 35% Telemetry, 25% Surveillance)
// ============================================================================

TEST_CASE("Stress: mixed workload", "[stress][codec][mixed]") {
    auto ping_wire  = encode_stress(make_ping());
    auto telem_wire = encode_stress(make_telemetry());
    auto surv_wire  = encode_stress(make_surveillance_typical());

    // Pre-generate random type indices: 0=Ping, 1=Telemetry, 2=Surveillance
    std::mt19937 rng(42);
    std::discrete_distribution<int> dist({40.0, 35.0, 25.0});
    std::vector<int> type_indices(STRESS_BATCH);
    for (auto& idx : type_indices) idx = dist(rng);

    const std::vector<const std::vector<uint8_t>*> frames = {
        &ping_wire, &telem_wire, &surv_wire
    };

    BENCHMARK_ADVANCED("100K mixed decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < STRESS_BATCH; ++i) {
                const auto& data = *frames[static_cast<size_t>(type_indices[i])];
                auto result = stress::StressFrame::decode_bytes(data);
                if (result) ++decoded;
            }
            return decoded;
        });
    };
}

// ============================================================================
// Stress: pipeline (encode 1000 into contiguous buffer, then decode all)
// ============================================================================

TEST_CASE("Stress: pipeline", "[stress][codec][pipeline]") {
    constexpr size_t PIPELINE_BATCH = 1000;
    auto ping  = make_ping();
    auto telem = make_telemetry();
    auto surv  = make_surveillance_typical();

    // Pre-encode one of each to know wire sizes
    auto ping_wire  = encode_stress(ping);
    auto telem_wire = encode_stress(telem);
    auto surv_wire  = encode_stress(surv);

    BENCHMARK_ADVANCED("1000x Ping pipeline (encode+decode stream)")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            // Encode into contiguous buffer
            std::vector<uint8_t> stream;
            stream.reserve(ping_wire.size() * PIPELINE_BATCH);
            for (size_t i = 0; i < PIPELINE_BATCH; ++i) {
                auto bytes = encode_stress(ping);
                stream.insert(stream.end(), bytes.begin(), bytes.end());
            }
            // Decode all from stream
            size_t offset = 0;
            size_t decoded = 0;
            while (offset < stream.size()) {
                auto span = std::span<const uint8_t>(stream.data() + offset, stream.size() - offset);
                // Read frame length from header: [msg-type:1][length:2]
                if (span.size() < 3) break;
                uint16_t frame_len = static_cast<uint16_t>((span[1] << 8) | span[2]);
                if (frame_len > span.size()) break;
                auto result = stress::StressFrame::decode_bytes(span.subspan(0, frame_len));
                if (result) ++decoded;
                offset += frame_len;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("1000x TelemetryReport pipeline (encode+decode stream)")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            std::vector<uint8_t> stream;
            stream.reserve(telem_wire.size() * PIPELINE_BATCH);
            for (size_t i = 0; i < PIPELINE_BATCH; ++i) {
                auto bytes = encode_stress(telem);
                stream.insert(stream.end(), bytes.begin(), bytes.end());
            }
            size_t offset = 0;
            size_t decoded = 0;
            while (offset < stream.size()) {
                auto span = std::span<const uint8_t>(stream.data() + offset, stream.size() - offset);
                if (span.size() < 3) break;
                uint16_t frame_len = static_cast<uint16_t>((span[1] << 8) | span[2]);
                if (frame_len > span.size()) break;
                auto result = stress::StressFrame::decode_bytes(span.subspan(0, frame_len));
                if (result) ++decoded;
                offset += frame_len;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("1000x Surveillance pipeline (encode+decode stream)")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            std::vector<uint8_t> stream;
            stream.reserve(surv_wire.size() * PIPELINE_BATCH);
            for (size_t i = 0; i < PIPELINE_BATCH; ++i) {
                auto bytes = encode_stress(surv);
                stream.insert(stream.end(), bytes.begin(), bytes.end());
            }
            size_t offset = 0;
            size_t decoded = 0;
            while (offset < stream.size()) {
                auto span = std::span<const uint8_t>(stream.data() + offset, stream.size() - offset);
                if (span.size() < 3) break;
                uint16_t frame_len = static_cast<uint16_t>((span[1] << 8) | span[2]);
                if (frame_len > span.size()) break;
                auto result = stress::StressFrame::decode_bytes(span.subspan(0, frame_len));
                if (result) ++decoded;
                offset += frame_len;
            }
            return decoded;
        });
    };
}
