// SPDX-License-Identifier: MIT
// Conduit - Codec encode/decode benchmarks
//
// Measures per-message encode, decode, and roundtrip latency for
// ASTERIX CAT001, CAT048, CAT253 and sentry_link message types.

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include <asterix/messages.hpp>
#include <asterix/sessions.hpp>
#include <sentry_link/messages.hpp>
#include <sentry_link/sessions.hpp>

// ============================================================================
// Message builders — realistic field values
// ============================================================================

namespace {

// --- ASTERIX ---

asterix::Cat001Record make_cat001_minimal() {
    asterix::Cat001Record rec;
    auto& it = rec.mutable_items();

    asterix::DataSourceId ds;
    ds.set_sac(1);
    ds.set_sic(2);
    it.set_i010(ds);

    asterix::time_of_day tod;
    tod.set_value(43200.0); // noon UTC
    it.set_i141(tod);

    return rec;
}

asterix::Cat001Record make_cat001_typical() {
    asterix::Cat001Record rec;
    auto& it = rec.mutable_items();

    asterix::DataSourceId ds;
    ds.set_sac(10);
    ds.set_sic(20);
    it.set_i010(ds);

    asterix::i020 trd;
    trd.set_typ(asterix::cat001_report_type::track);
    it.set_i020(trd);

    asterix::PolarRhoTheta pos;
    pos.set_rho(12500.0);
    pos.set_theta(180.0);
    it.set_i040(pos);

    asterix::i070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(0);
    mode3a.set_code(01234);
    it.set_i070(mode3a);

    asterix::Cat001TrackStatus ts;
    ts.set_cnf(1);
    ts.set_rad(2);
    ts.set_dou(0);
    ts.set_man(0);
    ts.set_rdp(1);
    ts.set_gho(0);
    it.set_i170(ts);

    asterix::time_of_day tod;
    tod.set_value(43200.0);
    it.set_i141(tod);

    return rec;
}

asterix::Cat048Record make_cat048_typical() {
    asterix::Cat048Record rec;
    auto& it = rec.mutable_items();

    asterix::DataSourceId ds;
    ds.set_sac(5);
    ds.set_sic(100);
    it.set_i010(ds);

    asterix::time_of_day tod;
    tod.set_value(36000.0); // 10h UTC
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

    asterix::itemsi070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(0);
    mode3a.set_code(07700);
    it.set_i070(mode3a);

    asterix::itemsi090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(350.0);
    it.set_i090(fl);

    it.set_i220(0xABCDEF); // Aircraft address

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

asterix::Cat253Record make_cat253_format_a() {
    asterix::Cat253Record rec;
    auto& it = rec.mutable_items();

    asterix::DataSourceId ds;
    ds.set_sac(3);
    ds.set_sic(42);
    it.set_i010(ds);

    asterix::time_of_day tod;
    tod.set_value(7200.0);
    it.set_i020(tod);

    it.set_i030(static_cast<asterix::uint16>(1001));

    asterix::Cat253I040 mt;
    mt.set_msg_type(asterix::cat253_msg_type::data);
    it.set_i040(mt);

    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(100); // range 0-255 -> FormatA
    it.set_i080(ctrl);

    asterix::Cat253I100FormatA fa;
    fa.set_data_type(1);
    std::array<uint8_t, 16> payload{};
    for (size_t i = 0; i < 16; ++i) payload[i] = static_cast<uint8_t>(i + 1);
    fa.set_payload(payload);
    it.set_i100(fa);

    return rec;
}

asterix::Cat253Record make_cat253_format_d() {
    asterix::Cat253Record rec;
    auto& it = rec.mutable_items();

    asterix::DataSourceId ds;
    ds.set_sac(7);
    ds.set_sic(88);
    it.set_i010(ds);

    asterix::time_of_day tod;
    tod.set_value(14400.0);
    it.set_i020(tod);

    it.set_i030(static_cast<asterix::uint16>(2002));

    asterix::Cat253I040 mt;
    mt.set_msg_type(asterix::cat253_msg_type::data);
    it.set_i040(mt);

    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(1500); // range 1024-2047 -> FormatD
    it.set_i080(ctrl);

    asterix::Cat253I100FormatD fd;
    asterix::wgs84_fine lat;
    lat.set_value(41.0);
    fd.set_latitude(lat);
    asterix::wgs84_fine lon;
    lon.set_value(29.0);
    fd.set_longitude(lon);
    fd.set_altitude(10000.0);
    fd.set_ground_speed(250.0);
    fd.set_heading(180.0);
    it.set_i100(fd);

    return rec;
}

// --- Sentry Link ---

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
    sb.set_unit_code(1); // celsius
    return sb;
}

sentry_link::ConfigBody make_config() {
    sentry_link::ConfigBody cb;
    cb.set_device_name("sensor-node-01");
    sentry_link::FirmwareVersion fw;
    fw.set_major(2);
    fw.set_minor(5);
    fw.set_patch(1024);
    cb.set_firmware(fw);
    cb.set_mode(sentry_link::device_mode::active);
    cb.set_log_level(3);
    cb.set_auto_report(1);
    cb.set_compression(0);
    cb.set_sample_rate(1000);
    return cb;
}

// Encode a message through AsterixFrame::wrap + encode_bytes
template<typename T>
std::vector<uint8_t> encode_asterix(const T& msg) {
    auto frame = asterix::AsterixFrame::wrap(msg);
    return frame.encode_bytes().value();
}

// Encode a sentry_link message through Frame::wrap + encode_bytes
template<typename T>
std::vector<uint8_t> encode_sentry(const T& msg) {
    auto frame = sentry_link::Frame::wrap(msg);
    return frame.encode_bytes().value();
}

} // namespace

// ============================================================================
// Codec: encode
// ============================================================================

TEST_CASE("Codec: encode", "[benchmark][codec][encode]") {
    auto cat001_min = make_cat001_minimal();
    auto cat001_typ = make_cat001_typical();
    auto cat048     = make_cat048_typical();
    auto cat253a    = make_cat253_format_a();
    auto cat253d    = make_cat253_format_d();
    auto heartbeat  = make_heartbeat();
    auto alert      = make_alert();
    auto sensor     = make_sensor();
    auto config     = make_config();

    BENCHMARK("encode CAT001 minimal")  { return encode_asterix(cat001_min); };
    BENCHMARK("encode CAT001 typical")  { return encode_asterix(cat001_typ); };
    BENCHMARK("encode CAT048 typical")  { return encode_asterix(cat048); };
    BENCHMARK("encode CAT253 FormatA")  { return encode_asterix(cat253a); };
    BENCHMARK("encode CAT253 FormatD")  { return encode_asterix(cat253d); };
    BENCHMARK("encode sentry Heartbeat"){ return encode_sentry(heartbeat); };
    BENCHMARK("encode sentry Alert")    { return encode_sentry(alert); };
    BENCHMARK("encode sentry Sensor")   { return encode_sentry(sensor); };
    BENCHMARK("encode sentry Config")   { return encode_sentry(config); };
}

// ============================================================================
// Codec: decode
// ============================================================================

TEST_CASE("Codec: decode", "[benchmark][codec][decode]") {
    auto cat001_min_bytes = encode_asterix(make_cat001_minimal());
    auto cat001_typ_bytes = encode_asterix(make_cat001_typical());
    auto cat048_bytes     = encode_asterix(make_cat048_typical());
    auto cat253a_bytes    = encode_asterix(make_cat253_format_a());
    auto cat253d_bytes    = encode_asterix(make_cat253_format_d());
    auto heartbeat_bytes  = encode_sentry(make_heartbeat());
    auto alert_bytes      = encode_sentry(make_alert());
    auto sensor_bytes     = encode_sentry(make_sensor());
    auto config_bytes     = encode_sentry(make_config());

    BENCHMARK("decode CAT001 minimal") {
        return asterix::AsterixFrame::decode_bytes(cat001_min_bytes);
    };
    BENCHMARK("decode CAT001 typical") {
        return asterix::AsterixFrame::decode_bytes(cat001_typ_bytes);
    };
    BENCHMARK("decode CAT048 typical") {
        return asterix::AsterixFrame::decode_bytes(cat048_bytes);
    };
    BENCHMARK("decode CAT253 FormatA") {
        return asterix::AsterixFrame::decode_bytes(cat253a_bytes);
    };
    BENCHMARK("decode CAT253 FormatD") {
        return asterix::AsterixFrame::decode_bytes(cat253d_bytes);
    };
    BENCHMARK("decode sentry Heartbeat") {
        return sentry_link::Frame::decode_bytes(heartbeat_bytes);
    };
    BENCHMARK("decode sentry Alert") {
        return sentry_link::Frame::decode_bytes(alert_bytes);
    };
    BENCHMARK("decode sentry Sensor") {
        return sentry_link::Frame::decode_bytes(sensor_bytes);
    };
    BENCHMARK("decode sentry Config") {
        return sentry_link::Frame::decode_bytes(config_bytes);
    };
}

// ============================================================================
// Codec: roundtrip (encode + decode)
// ============================================================================

TEST_CASE("Codec: roundtrip", "[benchmark][codec][roundtrip]") {
    auto cat001_typ = make_cat001_typical();
    auto cat048     = make_cat048_typical();
    auto cat253a    = make_cat253_format_a();
    auto cat253d    = make_cat253_format_d();
    auto heartbeat  = make_heartbeat();
    auto alert      = make_alert();
    auto sensor     = make_sensor();
    auto config     = make_config();

    BENCHMARK("roundtrip CAT001 typical") {
        auto bytes = encode_asterix(cat001_typ);
        return asterix::AsterixFrame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip CAT048 typical") {
        auto bytes = encode_asterix(cat048);
        return asterix::AsterixFrame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip CAT253 FormatA") {
        auto bytes = encode_asterix(cat253a);
        return asterix::AsterixFrame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip CAT253 FormatD") {
        auto bytes = encode_asterix(cat253d);
        return asterix::AsterixFrame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip sentry Heartbeat") {
        auto bytes = encode_sentry(heartbeat);
        return sentry_link::Frame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip sentry Alert") {
        auto bytes = encode_sentry(alert);
        return sentry_link::Frame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip sentry Sensor") {
        auto bytes = encode_sentry(sensor);
        return sentry_link::Frame::decode_bytes(bytes);
    };
    BENCHMARK("roundtrip sentry Config") {
        auto bytes = encode_sentry(config);
        return sentry_link::Frame::decode_bytes(bytes);
    };
}
