// SPDX-License-Identifier: MIT
// Conduit - Memory profiling benchmarks
//
// Measures sizeof() for key conduit types, encoded wire sizes, and
// wire-to-memory ratios.

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/transceiver/handler.hpp>
#include <conduit/transceiver/stream_framer.hpp>
#include <conduit/traits/session_traits.hpp>

#include <asterix/messages.hpp>
#include <asterix/sessions.hpp>
#include <sentry_link/messages.hpp>
#include <sentry_link/sessions.hpp>

#include <cstdio>
#include <variant>

// ============================================================================
// Message factories
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

} // namespace

// ============================================================================
// Type sizes (sizeof)
// ============================================================================

TEST_CASE("Memory: type sizes", "[memory][sizeof]") {
    auto sentry_session  = sentry_link::create_frame_session();

    std::fprintf(stdout, "\nMemory: type sizes\n");
    std::fprintf(stdout, "-------------------------------------------\n");
    std::fprintf(stdout, "  sizeof FrameSession            = %zu bytes\n", sizeof(*sentry_session));
    std::fprintf(stdout, "  sizeof BitWriter               = %zu bytes\n", sizeof(conduit::io::BitWriter));
    std::fprintf(stdout, "  sizeof BitReader               = %zu bytes\n", sizeof(conduit::io::BitReader));
    std::fprintf(stdout, "  sizeof StreamFramer            = %zu bytes\n", sizeof(conduit::transceiver::StreamFramer));
    std::fprintf(stdout, "  sizeof HandlerRegistry         = %zu bytes\n", sizeof(conduit::transceiver::HandlerRegistry));
    std::fprintf(stdout, "  sizeof Cat001Record            = %zu bytes\n", sizeof(asterix::Cat001Record));
    std::fprintf(stdout, "  sizeof Cat048Record            = %zu bytes\n", sizeof(asterix::Cat048Record));
    std::fprintf(stdout, "  sizeof Cat253Record            = %zu bytes\n", sizeof(asterix::Cat253Record));
    std::fprintf(stdout, "  sizeof HeartbeatBody           = %zu bytes\n", sizeof(sentry_link::HeartbeatBody));
    std::fprintf(stdout, "  sizeof AlertBody               = %zu bytes\n", sizeof(sentry_link::AlertBody));
    std::fprintf(stdout, "  sizeof SensorBody              = %zu bytes\n", sizeof(sentry_link::SensorBody));
    std::fprintf(stdout, "  sizeof ConfigBody              = %zu bytes\n", sizeof(sentry_link::ConfigBody));
    std::fprintf(stdout, "  sizeof AsterixFrame            = %zu bytes\n", sizeof(asterix::AsterixFrame));
    std::fprintf(stdout, "  sizeof sentry_link::Frame      = %zu bytes\n", sizeof(sentry_link::Frame));
    std::fprintf(stdout, "\n");

    REQUIRE(sizeof(conduit::io::BitWriter) > 0);
    REQUIRE(sizeof(conduit::io::BitReader) > 0);
}

// ============================================================================
// Wire sizes (encoded message sizes)
// ============================================================================

TEST_CASE("Memory: wire sizes", "[memory][wire]") {
    auto cat048_bytes    = wrap_cat048(make_cat048()).encode_bytes().value();
    auto heartbeat_bytes = sentry_link::Frame::wrap(make_heartbeat()).encode_bytes().value();
    auto alert_bytes     = sentry_link::Frame::wrap(make_alert()).encode_bytes().value();

    std::fprintf(stdout, "\nMemory: wire sizes\n");
    std::fprintf(stdout, "-------------------------------------------\n");
    std::fprintf(stdout, "  wire size CAT048 typical       = %zu bytes\n", cat048_bytes.size());
    std::fprintf(stdout, "  wire size sentry Heartbeat     = %zu bytes\n", heartbeat_bytes.size());
    std::fprintf(stdout, "  wire size sentry Alert         = %zu bytes\n", alert_bytes.size());
    std::fprintf(stdout, "  in-memory Cat048Record         = %zu bytes\n", sizeof(asterix::Cat048Record));
    std::fprintf(stdout, "  in-memory HeartbeatBody        = %zu bytes\n", sizeof(sentry_link::HeartbeatBody));
    std::fprintf(stdout, "  in-memory AlertBody            = %zu bytes\n", sizeof(sentry_link::AlertBody));
    std::fprintf(stdout, "  ratio CAT048  wire/memory      = %.2f\n",
                 static_cast<double>(cat048_bytes.size()) / sizeof(asterix::Cat048Record));
    std::fprintf(stdout, "  ratio Heartbeat wire/memory    = %.2f\n",
                 static_cast<double>(heartbeat_bytes.size()) / sizeof(sentry_link::HeartbeatBody));
    std::fprintf(stdout, "\n");

    REQUIRE(cat048_bytes.size() > 0);
    REQUIRE(heartbeat_bytes.size() > 0);
}

// ============================================================================
// Encode allocation benchmark (measures encode with allocation overhead)
// ============================================================================

TEST_CASE("Memory: encode allocation overhead", "[benchmark][memory]") {
    auto cat048 = make_cat048();
    auto hb     = make_heartbeat();

    BENCHMARK("CAT048 encode (with alloc)") {
        auto frame = wrap_cat048(cat048);
        return frame.encode_bytes().value();
    };

    BENCHMARK("sentry Heartbeat encode (with alloc)") {
        auto frame = sentry_link::Frame::wrap(hb);
        return frame.encode_bytes().value();
    };
}
