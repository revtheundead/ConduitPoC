// SPDX-License-Identifier: MIT
// Conduit - Sustained throughput benchmarks
//
// Pre-allocates large batches of messages (10000) and measures total
// encode/decode throughput in messages/sec and MB/s. Uses BENCHMARK_ADVANCED
// to exclude setup from measurement.

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include <asterix/messages.hpp>
#include <asterix/sessions.hpp>
#include <sentry_link/messages.hpp>
#include <sentry_link/sessions.hpp>

#include <random>
#include <variant>
#include <vector>

static constexpr size_t BATCH_SIZE = 10000;

// ============================================================================
// Message factories (same as bench_codec.cpp, kept local to avoid coupling)
// ============================================================================

namespace {

asterix::Cat048Record make_cat048() {
    asterix::Cat048Record rec;
    auto& it = rec.mutable_items();

    asterix::DataSourceId ds;
    ds.set_sac(5);
    ds.set_sic(100);
    it.set_i010(ds);

    asterix::time_of_day tod;
    tod.set_value(36000.0);
    it.set_i140(tod);

    asterix::Cat048TargetReportDescriptor trd;
    trd.set_typ(3);
    trd.set_sim(0);
    trd.set_rdp(0);
    trd.set_spi(0);
    trd.set_rab(0);
    it.set_i020(trd);

    asterix::PolarRhoTheta pos;
    pos.set_rho(25000.0);
    pos.set_theta(90.0);
    it.set_i040(pos);

    asterix::items_i070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(0);
    mode3a.set_code(07700);
    it.set_i070(mode3a);

    asterix::items_i090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(350.0);
    it.set_i090(fl);

    it.set_i220(0xABCDEF);

    asterix::aircraft_ident ident;
    ident.set_value("BAW123");
    it.set_i240(ident);

    it.set_i161(static_cast<asterix::track_number>(4567));

    asterix::CartesianXY cart;
    cart.set_x(1234);
    cart.set_y(-5678);
    it.set_i042(cart);

    asterix::i200 vel;
    vel.set_ground_speed(0.25);
    vel.set_heading(45.0);
    it.set_i200(vel);

    asterix::Cat048TrackStatus ts;
    ts.set_cnf(1);
    ts.set_rad(2);
    ts.set_dou(0);
    ts.set_mah(0);
    ts.set_cdm(0);
    it.set_i170(ts);

    return rec;
}

asterix::AsterixFrame wrap_cat048(const asterix::Cat048Record& rec) {
    asterix::DataBlock db;
    db.set_cat(asterix::CAT048);
    asterix::DataBlock_cat048 cat_recs;
    cat_recs.mutable_items().push_back(rec);
    db.set_records(asterix::recordsVariant{cat_recs});
    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) { (void)v.encode(lw); }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));
    asterix::AsterixFrame frame;
    frame.mutable_blocks().push_back(db);
    return frame;
}

sentry_link::HeartbeatBody make_heartbeat() {
    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(1700000000);
    hb.set_uptime_hours(720);
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(65);
    return hb;
}

sentry_link::AlertBody make_alert() {
    sentry_link::AlertBody ab;
    ab.set_timestamp(1700000100);
    ab.set_source_id(42);
    ab.set_severity(sentry_link::severity_level::warning);
    ab.set_category(5);
    ab.set_alert_code(1001);
    ab.set_message("Sensor temperature exceeds limit");
    return ab;
}

sentry_link::SensorBody make_sensor() {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(101);
    sb.set_timestamp(1700000200);
    sentry_link::SensorFlags flags;
    flags.set_channel(3);
    flags.set_precision(2);
    flags.set_saturated(0);
    flags.set_valid(1);
    sb.set_flags(flags);
    sb.set_raw_value(23.45);
    sb.set_unit_code(1);
    return sb;
}

} // namespace

// ============================================================================
// Batch encode throughput
// ============================================================================

TEST_CASE("Throughput: batch encode", "[benchmark][throughput][encode]") {
    auto cat048 = make_cat048();
    auto hb     = make_heartbeat();
    auto alert  = make_alert();
    auto sensor = make_sensor();

    BENCHMARK_ADVANCED("10000x CAT048 encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto frame = wrap_cat048(cat048);
                auto bytes = frame.encode_bytes().value();
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };

    BENCHMARK_ADVANCED("10000x sentry Heartbeat encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto frame = sentry_link::Frame::wrap(hb);
                auto bytes = frame.encode_bytes().value();
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };

    BENCHMARK_ADVANCED("10000x sentry Alert encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto frame = sentry_link::Frame::wrap(alert);
                auto bytes = frame.encode_bytes().value();
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };

    BENCHMARK_ADVANCED("10000x sentry Sensor encode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t total_bytes = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto frame = sentry_link::Frame::wrap(sensor);
                auto bytes = frame.encode_bytes().value();
                total_bytes += bytes.size();
            }
            return total_bytes;
        });
    };
}

// ============================================================================
// Batch decode throughput
// ============================================================================

TEST_CASE("Throughput: batch decode", "[benchmark][throughput][decode]") {
    // Pre-encode all messages
    std::vector<std::vector<uint8_t>> cat048_frames(BATCH_SIZE);
    std::vector<std::vector<uint8_t>> heartbeat_frames(BATCH_SIZE);
    std::vector<std::vector<uint8_t>> alert_frames(BATCH_SIZE);
    std::vector<std::vector<uint8_t>> sensor_frames(BATCH_SIZE);

    {
        auto cat048 = make_cat048();
        auto hb     = make_heartbeat();
        auto alert  = make_alert();
        auto sensor = make_sensor();
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            cat048_frames[i]    = wrap_cat048(cat048).encode_bytes().value();
            heartbeat_frames[i] = sentry_link::Frame::wrap(hb).encode_bytes().value();
            alert_frames[i]     = sentry_link::Frame::wrap(alert).encode_bytes().value();
            sensor_frames[i]    = sentry_link::Frame::wrap(sensor).encode_bytes().value();
        }
    }

    BENCHMARK_ADVANCED("10000x CAT048 decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto result = asterix::AsterixFrame::decode_bytes(cat048_frames[i]);
                if (result) ++decoded;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("10000x sentry Heartbeat decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto result = sentry_link::Frame::decode_bytes(heartbeat_frames[i]);
                if (result) ++decoded;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("10000x sentry Alert decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto result = sentry_link::Frame::decode_bytes(alert_frames[i]);
                if (result) ++decoded;
            }
            return decoded;
        });
    };

    BENCHMARK_ADVANCED("10000x sentry Sensor decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                auto result = sentry_link::Frame::decode_bytes(sensor_frames[i]);
                if (result) ++decoded;
            }
            return decoded;
        });
    };
}

// ============================================================================
// Mixed workload: random message type selection
// ============================================================================

TEST_CASE("Throughput: mixed workload", "[benchmark][throughput][mixed]") {
    // Pre-encode all message types
    auto cat048_bytes    = wrap_cat048(make_cat048()).encode_bytes().value();
    auto heartbeat_bytes = sentry_link::Frame::wrap(make_heartbeat()).encode_bytes().value();
    auto alert_bytes     = sentry_link::Frame::wrap(make_alert()).encode_bytes().value();
    auto sensor_bytes    = sentry_link::Frame::wrap(make_sensor()).encode_bytes().value();

    // Pre-generate random indices for message type selection
    std::mt19937 rng(42); // deterministic seed
    std::uniform_int_distribution<int> dist(0, 3);
    std::vector<int> type_indices(BATCH_SIZE);
    for (auto& idx : type_indices) idx = dist(rng);

    const std::vector<std::vector<uint8_t>*> frames = {
        &cat048_bytes, &heartbeat_bytes, &alert_bytes, &sensor_bytes
    };

    BENCHMARK_ADVANCED("10000x mixed decode")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&] {
            size_t decoded = 0;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                const auto& data = *frames[static_cast<size_t>(type_indices[i])];
                if (type_indices[i] == 0) {
                    auto result = asterix::AsterixFrame::decode_bytes(data);
                    if (result) ++decoded;
                } else {
                    auto result = sentry_link::Frame::decode_bytes(data);
                    if (result) ++decoded;
                }
            }
            return decoded;
        });
    };
}
