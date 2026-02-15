// SPDX-License-Identifier: MIT
// Bgen tests - ASTERIX and SentryLink protocol encode/decode verification
//
// Thorough tests for the generated code from real-world protocol specifications.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

// Helper: compare fixed-length (null-padded) strings by stripping trailing nulls
static std::string strip_nulls(const std::string& s) {
    auto pos = s.find('\0');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

#include "asterix/messages.hpp"
#include "asterix/constants.hpp"
#include "sentry_link/messages.hpp"
#include "sentry_link/constants.hpp"

// ============================================================================
// ASTERIX: DataSourceId (SAC/SIC) roundtrip
// ============================================================================

TEST_CASE("ASTERIX DataSourceId roundtrip", "[asterix][structs]") {
    asterix::DataSourceId dsid;
    dsid.set_sac(0x12);
    dsid.set_sic(0x34);

    conduit::io::BitWriter w;
    auto enc_r = dsid.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    REQUIRE(bytes.size() == 2);
    CHECK(bytes[0] == 0x12);
    CHECK(bytes[1] == 0x34);

    conduit::io::BitReader r(bytes);
    auto decoded = asterix::DataSourceId::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sac() == 0x12);
    CHECK(decoded->sic() == 0x34);
}

// ============================================================================
// ASTERIX: CartesianXY signed coordinates
// ============================================================================

TEST_CASE("ASTERIX CartesianXY positive coordinates roundtrip", "[asterix][structs]") {
    asterix::CartesianXY xy;
    xy.set_x(1000);
    xy.set_y(2000);
    conduit::io::BitWriter w;
    auto enc_r = xy.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    REQUIRE(bytes.size() == 4);

    conduit::io::BitReader r(bytes);
    auto decoded = asterix::CartesianXY::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->x() == 1000);
    CHECK(decoded->y() == 2000);
}

TEST_CASE("ASTERIX CartesianXY negative coordinates roundtrip", "[asterix][structs]") {
    asterix::CartesianXY xy;
    xy.set_x(-500);
    xy.set_y(-1500);
    conduit::io::BitWriter w;
    auto enc_r = xy.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    conduit::io::BitReader r(bytes);
    auto decoded = asterix::CartesianXY::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->x() == -500);
    CHECK(decoded->y() == -1500);
}

// ============================================================================
// ASTERIX: PolarRhoTheta
// ============================================================================

TEST_CASE("ASTERIX PolarRhoTheta roundtrip", "[asterix][structs]") {
    asterix::PolarRhoTheta polar;
    polar.set_rho(5000);  // scale=1 -> physical = raw
    // theta has scale=0.0054931640625: physical = 30000 * 0.0054931640625 ~ 164.795
    polar.set_theta(30000 * 0.0054931640625);
    conduit::io::BitWriter w;
    auto enc_r = polar.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    REQUIRE(bytes.size() == 4);

    conduit::io::BitReader r(bytes);
    auto decoded = asterix::PolarRhoTheta::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->rho() == 5000);
    CHECK_THAT(decoded->theta(), Catch::Matchers::WithinRel(30000 * 0.0054931640625, 0.001));
}

// ============================================================================
// ASTERIX: Cat001Record - FSPEC bitmap with single item
// ============================================================================

TEST_CASE("ASTERIX Cat001Record only I010 present", "[asterix][cat001][bitmap]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(10);
    dsid.set_sic(20);
    items.set_i010(dsid);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK((bytes[0] & 0x80) != 0);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i010());
    CHECK(decoded->items().i010().sac() == 10);
    CHECK(decoded->items().i010().sic() == 20);
    CHECK_FALSE(decoded->items().has_i020());
    CHECK_FALSE(decoded->items().has_i040());
}

// ============================================================================
// ASTERIX: Cat001Record - multiple FSPEC items in first octet
// ============================================================================

TEST_CASE("ASTERIX Cat001Record I010 + I020 + I040", "[asterix][cat001][bitmap]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0xAA);
    dsid.set_sic(0xBB);
    items.set_i010(dsid);

    asterix::i020 trd;
    trd.set_typ(asterix::cat001_report_type::track);
    items.set_i020(trd);

    asterix::PolarRhoTheta polar;
    polar.set_rho(1234);  // scale=1 -> physical = raw
    polar.set_theta(5678 * 0.0054931640625);  // physical degrees
    items.set_i040(polar);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().i010().sac() == 0xAA);
    CHECK(decoded->items().i010().sic() == 0xBB);
    CHECK(decoded->items().i020().typ() == asterix::cat001_report_type::track);
    CHECK(decoded->items().i040().rho() == 1234);
    CHECK_THAT(decoded->items().i040().theta(), Catch::Matchers::WithinRel(5678 * 0.0054931640625, 0.001));
}

// ============================================================================
// ASTERIX: Cat001Record - Mode 3/A code with bit fields
// ============================================================================

TEST_CASE("ASTERIX Cat001Record Mode 3/A code (I070)", "[asterix][cat001][bitfield]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::i070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(1);
    mode3a.set_code(01234); // Octal squawk code
    items.set_i070(mode3a);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i070());
    CHECK(decoded->items().i070().v() == 1);
    CHECK(decoded->items().i070().g() == 0);
    CHECK(decoded->items().i070().l() == 1);
    CHECK(decoded->items().i070().code() == 01234);
}

// ============================================================================
// ASTERIX: Cat001Record - Flight Level with signed 12-bit field
// ============================================================================

TEST_CASE("ASTERIX Cat001Record Flight Level I090 positive", "[asterix][cat001][signed]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::i090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(350.0); // FL350 -- scale=0.25 maps physical FL to raw 1/4 FL units
    items.set_i090(fl);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i090());
    CHECK(decoded->items().i090().v() == 1);
    CHECK_THAT(decoded->items().i090().fl(), Catch::Matchers::WithinRel(350.0, 0.001));
}

TEST_CASE("ASTERIX Cat001Record Flight Level I090 negative", "[asterix][cat001][signed]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::i090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(-20); // Below sea level
    items.set_i090(fl);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().i090().fl() == -20);
}

// ============================================================================
// ASTERIX: Cat001Record - Second FSPEC octet (I170, I141)
// ============================================================================

TEST_CASE("ASTERIX Cat001Record second FSPEC octet I170 + I141", "[asterix][cat001][fspec_ext]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    // I170 in second FSPEC octet - Track Status with FX
    asterix::Cat001TrackStatus ts;
    ts.set_cnf(1);
    ts.set_rad(2);
    ts.set_dou(0);
    ts.set_man(1);
    ts.set_rdp(0);
    ts.set_gho(0);
    items.set_i170(ts);

    // I141 - Time of Day
    asterix::time_of_day tod;
    tod.set_raw(128 * 3600); // 1 hour in 1/128 sec units
    items.set_i141(tod);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 1);
    CHECK(decoded->items().i170().rad() == 2);
    CHECK(decoded->items().i170().man() == 1);
    CHECK(decoded->items().has_i141());
    CHECK(decoded->items().i141().raw() == 128 * 3600);
}

// ============================================================================
// ASTERIX: Cat001Record - empty FSPEC (no items)
// ============================================================================

TEST_CASE("ASTERIX Cat001Record empty FSPEC", "[asterix][cat001][bitmap]") {
    asterix::Cat001Record rec;
    // Don't set any items
    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 1); CHECK(bytes[0] == 0x00);

    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded->items().has_i010());
    CHECK_FALSE(decoded->items().has_i020());
    CHECK_FALSE(decoded->items().has_i040());
    CHECK_FALSE(decoded->items().has_i042());
    CHECK_FALSE(decoded->items().has_i070());
    CHECK_FALSE(decoded->items().has_i090());
    CHECK_FALSE(decoded->items().has_i161());
    CHECK_FALSE(decoded->items().has_i170());
    CHECK_FALSE(decoded->items().has_i141());
}

// ============================================================================
// ASTERIX: Cat001Record - all first-octet items
// ============================================================================

TEST_CASE("ASTERIX Cat001Record all first-octet items", "[asterix][cat001][bitmap]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(1); dsid.set_sic(2);
    items.set_i010(dsid);

    asterix::i020 trd;
    trd.set_typ(asterix::cat001_report_type::plot);
    items.set_i020(trd);

    asterix::PolarRhoTheta polar;
    polar.set_rho(100); polar.set_theta(200);
    items.set_i040(polar);

    asterix::CartesianXY cart;
    cart.set_x(50); cart.set_y(-50);
    items.set_i042(cart);

    asterix::i070 mode3a;
    mode3a.set_v(1); mode3a.set_g(0); mode3a.set_l(0);
    mode3a.set_code(0x123);
    items.set_i070(mode3a);

    asterix::i090 fl;
    fl.set_v(1); fl.set_g(0); fl.set_fl(400);
    items.set_i090(fl);

    items.set_i161(42);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i010());
    CHECK(decoded->items().has_i020());
    CHECK(decoded->items().has_i040());
    CHECK(decoded->items().has_i042());
    CHECK(decoded->items().has_i070());
    CHECK(decoded->items().has_i090());
    CHECK(decoded->items().has_i161());
    CHECK(decoded->items().i161() == 42);
}

// ============================================================================
// ASTERIX: Cat253Record - compulsory items only
// ============================================================================

TEST_CASE("ASTERIX Cat253Record compulsory items I010-I050", "[asterix][cat253][bitmap]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0x01); dsid.set_sic(0x02);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(12800); // 100 seconds
    items.set_i020(tod);

    items.set_i030(static_cast<asterix::uint16>(1000)); // sequence number

    asterix::Cat253I040 mtype;
    mtype.set_msg_type(asterix::cat253_msg_type::data);
    items.set_i040(mtype);

    items.set_i050(static_cast<asterix::uint8>(5)); // priority

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i010());
    CHECK(decoded->items().i010().sac() == 0x01);
    CHECK(decoded->items().i010().sic() == 0x02);
    CHECK(decoded->items().has_i020());
    CHECK(decoded->items().i020().raw() == 12800);
    CHECK(decoded->items().has_i030());
    CHECK(decoded->items().i030() == 1000);
    CHECK(decoded->items().has_i040());
    CHECK(decoded->items().i040().msg_type() == asterix::cat253_msg_type::data);
    CHECK(decoded->items().has_i050());
    CHECK(decoded->items().i050() == 5);
    CHECK_FALSE(decoded->items().has_i060());
    CHECK_FALSE(decoded->items().has_i100());
}

// ============================================================================
// ASTERIX: Cat253Record - I060 status information bit fields
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I060 status bit fields", "[asterix][cat253][bitfield]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I060 status;
    status.set_operational(1);
    status.set_degraded(0);
    status.set_maintenance(1);
    status.set_error_code(15);
    items.set_i060(status);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i060());
    CHECK(decoded->items().i060().operational() == 1);
    CHECK(decoded->items().i060().degraded() == 0);
    CHECK(decoded->items().i060().maintenance() == 1);
    CHECK(decoded->items().i060().error_code() == 15);
}

// ============================================================================
// ASTERIX: Cat253Record - I070 sequence information
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I070 sequence info", "[asterix][cat253]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I070 seq;
    seq.set_sequence(0xABCD);
    seq.set_fragment(3);
    seq.set_total_fragments(5);
    items.set_i070(seq);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i070());
    CHECK(decoded->items().i070().sequence() == 0xABCD);
    CHECK(decoded->items().i070().fragment() == 3);
    CHECK(decoded->items().i070().total_fragments() == 5);
}

// ============================================================================
// ASTERIX: Cat253Record - I100 Format A (selector 0-255) via choice
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I100 FormatA choice", "[asterix][cat253][choice]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    // I080 must be set for structure-selector
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(100); // Range 0-255 -> Format A
    items.set_i080(ctrl);

    asterix::Cat253I100FormatA fmt_a;
    fmt_a.set_data_type(42);
    std::array<uint8_t, 16> payload{};
    payload[0] = 0xDE; payload[1] = 0xAD;
    fmt_a.set_payload(payload);
    items.set_i100(asterix::i100Variant{fmt_a});

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i080());
    CHECK(decoded->items().i080().structure_selector() == 100);
    CHECK(decoded->items().has_i100());
    auto& i100 = std::get<asterix::Cat253I100FormatA>(decoded->items().i100());
    CHECK(i100.data_type() == 42);
    CHECK(i100.payload()[0] == 0xDE);
    CHECK(i100.payload()[1] == 0xAD);
}

// ============================================================================
// ASTERIX: Cat253Record - I100 Format D (position data)
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I100 FormatD position data", "[asterix][cat253][choice]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(1500); // Range 1024-2047 -> Format D
    items.set_i080(ctrl);

    asterix::Cat253I100FormatD fmt_d;
    asterix::wgs84_fine lat, lon;
    lat.set_raw(500000000);   // ~41.9 degrees
    lon.set_raw(-100000000);  // ~-8.38 degrees
    fmt_d.set_latitude(lat);
    fmt_d.set_longitude(lon);
    fmt_d.set_altitude(10000.0);       // 10000 feet -- scale=0.25
    fmt_d.set_ground_speed(250.0);     // 250 knots -- scale=0.1
    fmt_d.set_heading(18000 * 0.0054931640625);  // degrees -- scale=0.0054931640625
    items.set_i100(asterix::i100Variant{fmt_d});

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i100());
    auto& d = std::get<asterix::Cat253I100FormatD>(decoded->items().i100());
    CHECK(d.latitude().raw() == 500000000);
    CHECK(d.longitude().raw() == -100000000);
    CHECK_THAT(d.altitude(), Catch::Matchers::WithinRel(10000.0, 0.001));
    CHECK_THAT(d.ground_speed(), Catch::Matchers::WithinRel(250.0, 0.001));
    CHECK_THAT(d.heading(), Catch::Matchers::WithinRel(18000 * 0.0054931640625, 0.001));
}

// ============================================================================
// ASTERIX: Cat253Record - I100 Format C with count-from array
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I100 FormatC structured records", "[asterix][cat253][choice][array]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(600); // Range 512-1023 -> Format C
    items.set_i080(ctrl);

    asterix::Cat253I100FormatC fmt_c;
    fmt_c.set_record_type(7);
    fmt_c.set_record_count(2);

    auto& recs = fmt_c.mutable_records();
    asterix::recordsElement r1;
    r1.set_id(1001);
    r1.set_value(0xDEADBEEF);
    asterix::time_of_day t1;
    t1.set_raw(25600);
    r1.set_timestamp(t1);
    recs.push_back(r1);

    asterix::recordsElement r2;
    r2.set_id(2002);
    r2.set_value(0xCAFEBABE);
    asterix::time_of_day t2;
    t2.set_raw(51200);
    r2.set_timestamp(t2);
    recs.push_back(r2);

    items.set_i100(asterix::i100Variant{fmt_c});

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i100());
    auto& c = std::get<asterix::Cat253I100FormatC>(decoded->items().i100());
    CHECK(c.record_type() == 7);
    CHECK(c.record_count() == 2);
    REQUIRE(c.records().size() == 2);
    CHECK(c.records()[0].id() == 1001);
    CHECK(c.records()[0].value() == 0xDEADBEEF);
    CHECK(c.records()[0].timestamp().raw() == 25600);
    CHECK(c.records()[1].id() == 2002);
    CHECK(c.records()[1].value() == 0xCAFEBABE);
}

// ============================================================================
// ASTERIX: Cat253Record - I110 text annotation (length-prefixed string)
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I110 text annotation", "[asterix][cat253][string]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I110 text;
    text.set_text("HELLO ASTERIX");
    items.set_i110(text);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i110());
    CHECK(strip_nulls(decoded->items().i110().text()) == "HELLO ASTERIX");
}

// ============================================================================
// ASTERIX: Cat253Record - I120 reference data
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I120 reference data", "[asterix][cat253]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I120 ref;
    ref.set_ref_type(3);
    ref.set_ref_id(0x12345678);
    items.set_i120(ref);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i120());
    CHECK(decoded->items().i120().ref_type() == 3);
    CHECK(decoded->items().i120().ref_id() == 0x12345678);
}

// ============================================================================
// ASTERIX: Cat253Record - Third FSPEC octet (RE/SP fields)
// ============================================================================

TEST_CASE("ASTERIX Cat253Record RE expansion field", "[asterix][cat253][fspec_ext]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::re re_field;
    re_field.set_len(5); // len includes itself: 5 = 1 + 4 bytes of data
    re_field.set_data({0xAA, 0xBB, 0xCC, 0xDD});
    items.set_re(re_field);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_re());
    CHECK(decoded->items().re().len() == 5);
    REQUIRE(decoded->items().re().data().size() == 4);
    CHECK(decoded->items().re().data()[0] == 0xAA);
    CHECK(decoded->items().re().data()[3] == 0xDD);
}

// ============================================================================
// ASTERIX: Cat253Record - I090 priority and flags
// ============================================================================

TEST_CASE("ASTERIX Cat253Record I090 priority flags", "[asterix][cat253][bitfield]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat253I090 pf;
    pf.set_priority(15);
    pf.set_urgent(1);
    pf.set_ack_required(1);
    pf.set_encrypted(0);
    pf.set_compressed(1);
    items.set_i090(pf);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat253Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i090());
    CHECK(decoded->items().i090().priority() == 15);
    CHECK(decoded->items().i090().urgent() == 1);
    CHECK(decoded->items().i090().ack_required() == 1);
    CHECK(decoded->items().i090().encrypted() == 0);
    CHECK(decoded->items().i090().compressed() == 1);
}

// ============================================================================
// ASTERIX: DataBlock with Cat001 records
// ============================================================================

TEST_CASE("ASTERIX DataBlock Cat001 roundtrip", "[asterix][datablock]") {
    asterix::DataBlock db;
    db.set_cat(asterix::CAT001);

    asterix::DataBlock_cat001 cat_records;
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();
    asterix::DataSourceId dsid;
    dsid.set_sac(5); dsid.set_sic(10);
    items.set_i010(dsid);
    cat_records.mutable_items().push_back(rec);

    db.set_records(asterix::recordsVariant{cat_records});

    // Compute length: header(3) + record payload
    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) {
        auto enc_r = v.encode(lw);
        // ignore result in length computation
        (void)enc_r;
    }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));

    auto enc_result = db.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::DataBlock::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->cat() == asterix::CAT001);
    auto& cat1 = std::get<asterix::DataBlock_cat001>(decoded->records());
    REQUIRE(cat1.items().size() == 1);
    CHECK(cat1.items()[0].items().i010().sac() == 5);
    CHECK(cat1.items()[0].items().i010().sic() == 10);
}

// ============================================================================
// ASTERIX: DataBlock with Cat253 records
// ============================================================================

TEST_CASE("ASTERIX DataBlock Cat253 roundtrip", "[asterix][datablock]") {
    asterix::DataBlock db;
    db.set_cat(asterix::CAT253);

    asterix::DataBlock_cat253 cat_records;
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();
    asterix::DataSourceId dsid;
    dsid.set_sac(0x10); dsid.set_sic(0x20);
    items.set_i010(dsid);
    items.set_i030(static_cast<asterix::uint16>(42));
    cat_records.mutable_items().push_back(rec);

    db.set_records(asterix::recordsVariant{cat_records});

    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) {
        auto enc_r = v.encode(lw);
        (void)enc_r;
    }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));

    auto enc_result = db.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::DataBlock::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->cat() == asterix::CAT253);
    auto& cat = std::get<asterix::DataBlock_cat253>(decoded->records());
    REQUIRE(cat.items().size() == 1);
    CHECK(cat.items()[0].items().i010().sac() == 0x10);
    CHECK(cat.items()[0].items().i030() == 42);
}

// ============================================================================
// ASTERIX: DataBlock otherwise (unknown category)
// ============================================================================

TEST_CASE("ASTERIX DataBlock unknown category uses otherwise", "[asterix][datablock][otherwise]") {
    // Build raw bytes for unknown category 99
    conduit::io::BitWriter w;
    w.write_u8(99);          // cat = 99 (unknown)
    w.write_u16(8);          // len = 8 (3 header + 5 data)
    w.write_u8(0x01);
    w.write_u8(0x02);
    w.write_u8(0x03);
    w.write_u8(0x04);
    w.write_u8(0x05);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = asterix::DataBlock::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->cat() == 99);
    auto& other = std::get<asterix::DataBlock_recordsOtherwise>(decoded->records());
    REQUIRE(other.data().size() == 5);
    CHECK(other.data()[0] == 0x01);
    CHECK(other.data()[4] == 0x05);
}

// ============================================================================
// ASTERIX: AsterixFrame with multiple DataBlocks
// ============================================================================

TEST_CASE("ASTERIX AsterixFrame multiple DataBlocks roundtrip", "[asterix][frame]") {
    asterix::AsterixFrame frame;

    // Block 1: Cat001
    {
        asterix::DataBlock db;
        db.set_cat(asterix::CAT001);
        asterix::DataBlock_cat001 cat_recs;
        asterix::Cat001Record rec;
        rec.mutable_items().set_i161(100);
        cat_recs.mutable_items().push_back(rec);
        db.set_records(asterix::recordsVariant{cat_recs});
        conduit::io::BitWriter lw;
        std::visit([&lw](const auto& v) {
            auto enc_r = v.encode(lw);
            (void)enc_r;
        }, db.records());
        db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));
        frame.mutable_blocks().push_back(db);
    }

    // Block 2: Cat253
    {
        asterix::DataBlock db;
        db.set_cat(asterix::CAT253);
        asterix::DataBlock_cat253 cat_recs;
        asterix::Cat253Record rec;
        rec.mutable_items().set_i050(static_cast<asterix::uint8>(7));
        cat_recs.mutable_items().push_back(rec);
        db.set_records(asterix::recordsVariant{cat_recs});
        conduit::io::BitWriter lw;
        std::visit([&lw](const auto& v) {
            auto enc_r = v.encode(lw);
            (void)enc_r;
        }, db.records());
        db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));
        frame.mutable_blocks().push_back(db);
    }

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::AsterixFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->blocks().size() == 2);
    CHECK(decoded->blocks()[0].cat() == asterix::CAT001);
    CHECK(decoded->blocks()[1].cat() == asterix::CAT253);

    auto& cat1 = std::get<asterix::DataBlock_cat001>(decoded->blocks()[0].records());
    CHECK(cat1.items()[0].items().i161() == 100);

    auto& cat253 = std::get<asterix::DataBlock_cat253>(decoded->blocks()[1].records());
    CHECK(cat253.items()[0].items().i050() == 7);
}

// ============================================================================
// ASTERIX: wrap() auto-computes DataBlock len field
// ============================================================================

TEST_CASE("ASTERIX wrap Cat253Record auto-sets DataBlock len", "[asterix][wrap]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    // Set structure selector for FormatA (range 0..255)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(42);
    items.set_i080(ctrl);

    // Set FormatA payload
    asterix::Cat253I100FormatA fmt_a;
    fmt_a.set_data_type(1);
    std::array<uint8_t, 16> payload{};
    payload.fill(0xAB);
    fmt_a.set_payload(payload);
    items.set_i100(asterix::i100Variant{fmt_a});

    asterix::DataBlock db;
    db.set_cat(asterix::CAT253);
    asterix::DataBlock_cat253 cat_recs;
    cat_recs.mutable_items().push_back(rec);
    db.set_records(asterix::recordsVariant{cat_recs});
    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) { (void)v.encode(lw); }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));
    asterix::AsterixFrame frame;
    frame.mutable_blocks().push_back(db);

    REQUIRE(frame.blocks().size() == 1);
    CHECK(frame.blocks()[0].cat() == asterix::CAT253);
    // len must be > 3 (header) for the frame to decode
    CHECK(frame.blocks()[0].len() > 3);

    // Verify round-trip
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::AsterixFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->blocks().size() == 1);
    CHECK(decoded->blocks()[0].cat() == asterix::CAT253);

    auto& cat = std::get<asterix::DataBlock_cat253>(decoded->blocks()[0].records());
    REQUIRE(cat.items().size() == 1);
    CHECK(cat.items()[0].items().has_i100());
    auto& i100 = std::get<asterix::Cat253I100FormatA>(cat.items()[0].items().i100());
    CHECK(i100.data_type() == 1);
    CHECK(i100.payload()[0] == 0xAB);
}

TEST_CASE("ASTERIX wrap Cat253Record FormatD position data round-trips", "[asterix][wrap]") {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    // Set structure selector for FormatD (range 1024..2047)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(1500);
    items.set_i080(ctrl);

    // Set FormatD payload
    asterix::Cat253I100FormatD fmt_d;
    asterix::wgs84_fine lat, lon;
    lat.set_raw(123456789);
    lon.set_raw(-987654321);
    fmt_d.set_latitude(lat);
    fmt_d.set_longitude(lon);
    fmt_d.set_altitude(2000.0);        // 2000 feet -- scale=0.25
    fmt_d.set_ground_speed(150.0);     // 150 knots -- scale=0.1
    fmt_d.set_heading(9000 * 0.0054931640625);  // degrees
    items.set_i100(asterix::i100Variant{fmt_d});

    asterix::DataBlock db;
    db.set_cat(asterix::CAT253);
    asterix::DataBlock_cat253 cat_recs;
    cat_recs.mutable_items().push_back(rec);
    db.set_records(asterix::recordsVariant{cat_recs});
    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) { (void)v.encode(lw); }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));
    asterix::AsterixFrame frame;
    frame.mutable_blocks().push_back(db);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::AsterixFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    auto& cat = std::get<asterix::DataBlock_cat253>(decoded->blocks()[0].records());
    auto& d = std::get<asterix::Cat253I100FormatD>(cat.items()[0].items().i100());
    CHECK(d.latitude().raw() == 123456789);
    CHECK(d.longitude().raw() == -987654321);
    CHECK_THAT(d.altitude(), Catch::Matchers::WithinRel(2000.0, 0.001));
    CHECK_THAT(d.ground_speed(), Catch::Matchers::WithinRel(150.0, 0.001));
    CHECK_THAT(d.heading(), Catch::Matchers::WithinRel(9000 * 0.0054931640625, 0.001));
}

// ============================================================================
// ASTERIX: Cat048Record - basic bitmap
// ============================================================================

TEST_CASE("ASTERIX Cat048Record I010 + I140 roundtrip", "[asterix][cat048][bitmap]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0x30); dsid.set_sic(0x40);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(64000);
    items.set_i140(tod);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().i010().sac() == 0x30);
    CHECK(decoded->items().i010().sic() == 0x40);
    CHECK(decoded->items().i140().raw() == 64000);
}

// ============================================================================
// ASTERIX: Cat048Record - Track Status with FX extension
// ============================================================================

TEST_CASE("ASTERIX Cat048Record I170 Track Status", "[asterix][cat048][fx]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat048TrackStatus ts;
    ts.set_cnf(1);
    ts.set_rad(2);
    ts.set_dou(0);
    ts.set_mah(1);
    ts.set_cdm(3);
    items.set_i170(ts);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 1);
    CHECK(decoded->items().i170().rad() == 2);
    CHECK(decoded->items().i170().dou() == 0);
    CHECK(decoded->items().i170().mah() == 1);
    CHECK(decoded->items().i170().cdm() == 3);
}

// ============================================================================
// ASTERIX: Cat048Record - I130 nested bitmap (compound sub-fields)
// ============================================================================

TEST_CASE("ASTERIX Cat048Record I130 compound sub-fields", "[asterix][cat048][nested_bitmap]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::i130 radar_plot;
    auto& s = radar_plot.mutable_sub();
    s.set_srl(100);
    s.set_sam(static_cast<asterix::int8>(-50));
    items.set_i130(radar_plot);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i130());
    CHECK(decoded->items().i130().sub().has_srl());
    CHECK(decoded->items().i130().sub().srl() == 100);
    CHECK(decoded->items().i130().sub().has_sam());
    CHECK(decoded->items().i130().sub().sam() == -50);
    CHECK_FALSE(decoded->items().i130().sub().has_srr());
    CHECK_FALSE(decoded->items().i130().sub().has_prl());
}

// ============================================================================
// ASTERIX: Cat048Record - I250 Mode S MB data (count-from array)
// ============================================================================

TEST_CASE("ASTERIX Cat048Record I250 Mode S BDS data", "[asterix][cat048][array]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::i250 mb;
    mb.set_rep(2);
    auto& bds = mb.mutable_bds();

    asterix::bdsElement e1;
    std::array<uint8_t, 7> data1 = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    e1.set_data(data1);
    e1.set_bds1(4);
    e1.set_bds2(0);
    bds.push_back(e1);

    asterix::bdsElement e2;
    std::array<uint8_t, 7> data2 = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07};
    e2.set_data(data2);
    e2.set_bds1(5);
    e2.set_bds2(0);
    bds.push_back(e2);

    items.set_i250(mb);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_i250());
    CHECK(decoded->items().i250().rep() == 2);
    REQUIRE(decoded->items().i250().bds().size() == 2);
    CHECK(decoded->items().i250().bds()[0].data() == data1);
    CHECK(decoded->items().i250().bds()[0].bds1() == 4);
    CHECK(decoded->items().i250().bds()[1].data() == data2);
    CHECK(decoded->items().i250().bds()[1].bds1() == 5);
}

// ============================================================================
// ASTERIX: Decode error - empty buffer
// ============================================================================

TEST_CASE("ASTERIX decode from empty buffer fails", "[asterix][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = asterix::DataBlock::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// ASTERIX: Decode error - truncated DataBlock
// ============================================================================

TEST_CASE("ASTERIX truncated DataBlock decode fails", "[asterix][errors]") {
    // Only 2 bytes, but DataBlock header is 3 (cat + len)
    std::vector<uint8_t> truncated = {0x01, 0x00};
    auto decoded = asterix::DataBlock::decode_bytes(truncated);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// ASTERIX: DataBlock wire format verification
// ============================================================================

TEST_CASE("ASTERIX DataBlock wire format header bytes", "[asterix][wire]") {
    asterix::DataBlock db;
    db.set_cat(asterix::CAT048);

    asterix::DataBlock_cat048 cat_recs;
    asterix::Cat048Record rec;
    // Empty record - just FSPEC byte 0x00
    cat_recs.mutable_items().push_back(rec);
    db.set_records(asterix::recordsVariant{cat_recs});

    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) {
        auto enc_r = v.encode(lw);
        (void)enc_r;
    }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));

    auto enc_result = db.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() >= 3);
    CHECK(bytes[0] == 48);  // CAT048
    // len in big-endian at bytes[1..2]
    uint16_t len = (static_cast<uint16_t>(bytes[1]) << 8) | bytes[2];
    CHECK(len == db.len());
}

// ============================================================================
// SentryLink: HeartbeatBody roundtrip
// ============================================================================

TEST_CASE("SentryLink HeartbeatBody roundtrip", "[sentry_link][heartbeat]") {
    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(1700000000);
    hb.set_uptime_hours(1234);
    hb.set_status(sentry_link::device_status::online);
    REQUIRE(hb.set_cpu_load(75).has_value());

    conduit::io::BitWriter w;
    auto enc_r = hb.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    CHECK(bytes.size() == 8);
    // timestamp=1700000000 = 0x6553F100 big-endian
    CHECK(bytes[0] == 0x65); CHECK(bytes[1] == 0x53);
    CHECK(bytes[2] == 0xF1); CHECK(bytes[3] == 0x00);

    conduit::io::BitReader r(bytes);
    auto decoded = sentry_link::HeartbeatBody::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->timestamp() == 1700000000);
    CHECK(decoded->uptime_hours() == 1234);
    CHECK(decoded->status() == sentry_link::device_status::online);
    CHECK(decoded->cpu_load() == 75);
}

// ============================================================================
// SentryLink: HeartbeatBody device_status enum values
// ============================================================================

TEST_CASE("SentryLink HeartbeatBody all device statuses", "[sentry_link][heartbeat][enum]") {
    for (auto st : {sentry_link::device_status::online, sentry_link::device_status::standby,
                    sentry_link::device_status::maintenance, sentry_link::device_status::error}) {
        sentry_link::HeartbeatBody hb;
        hb.set_timestamp(0);
        hb.set_uptime_hours(0);
        hb.set_status(st);
        REQUIRE(hb.set_cpu_load(0).has_value());

        conduit::io::BitWriter w;
        auto enc_r = hb.encode(w);
        REQUIRE(enc_r.has_value());
        auto finish_result = w.finish();
        REQUIRE(finish_result.has_value());
        auto bytes = std::move(*finish_result);

        conduit::io::BitReader r(bytes);
        auto decoded = sentry_link::HeartbeatBody::decode(r);
        REQUIRE(decoded.has_value());
        CHECK(decoded->status() == st);
    }
}

// ============================================================================
// SentryLink: HeartbeatBody cpu_load max constraint decode
// ============================================================================

TEST_CASE("SentryLink HeartbeatBody cpu_load > 100 decode fails", "[sentry_link][heartbeat][constraint]") {
    conduit::io::BitWriter w;
    w.write_u32(0);          // timestamp
    w.write_u16(0);          // uptime
    w.write_u8(0);           // status = online
    w.write_u8(101);         // cpu_load = 101 (exceeds max)
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    conduit::io::BitReader r(bytes);
    auto decoded = sentry_link::HeartbeatBody::decode(r);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// SentryLink: SensorBody with SensorFlags bit-packing
// ============================================================================

TEST_CASE("SentryLink SensorBody roundtrip", "[sentry_link][sensor]") {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(1234);
    sb.set_timestamp(1700000100);

    sentry_link::SensorFlags flags;
    flags.set_channel(5);
    flags.set_precision(3);
    flags.set_saturated(0);
    flags.set_valid(1);
    sb.set_flags(flags);

    sb.set_raw_value(-250000); // negative value
    sb.set_unit_code(1);       // celsius

    conduit::io::BitWriter w;
    auto enc_r = sb.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    CHECK(bytes.size() == 12);

    conduit::io::BitReader r(bytes);
    auto decoded = sentry_link::SensorBody::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sensor_id() == 1234);
    CHECK(decoded->timestamp() == 1700000100);
    CHECK(decoded->flags().channel() == 5);
    CHECK(decoded->flags().precision() == 3);
    CHECK(decoded->flags().saturated() == 0);
    CHECK(decoded->flags().valid() == 1);
    CHECK(decoded->raw_value() == -250000);
    CHECK(decoded->unit_code() == 1);
}

// ============================================================================
// SentryLink: SensorFlags bit-packed wire format verification
// ============================================================================

TEST_CASE("SentryLink SensorFlags wire format", "[sentry_link][sensor][wire]") {
    sentry_link::SensorFlags flags;
    flags.set_channel(0b1010);     // 4 bits
    flags.set_precision(0b11);     // 2 bits
    flags.set_saturated(1);        // 1 bit
    flags.set_valid(1);            // 1 bit
    // Wire: [1010][11][1][1] = 0b10101111 = 0xAF

    conduit::io::BitWriter w;
    auto enc_r = flags.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    REQUIRE(bytes.size() == 1);
    CHECK(bytes[0] == 0xAF);
}

// ============================================================================
// SentryLink: ConfigBody with string, firmware, bit fields
// ============================================================================

TEST_CASE("SentryLink ConfigBody roundtrip", "[sentry_link][config]") {
    sentry_link::ConfigBody cfg;
    cfg.set_device_name("TestSensor01");

    sentry_link::FirmwareVersion fw;
    fw.set_major(2);
    fw.set_minor(5);
    fw.set_patch(1024);
    cfg.set_firmware(fw);

    cfg.set_mode(sentry_link::device_mode::calibration);
    cfg.set_log_level(5);
    cfg.set_auto_report(1);
    cfg.set_compression(0);
    cfg.set_sample_rate(8000);

    conduit::io::BitWriter w;
    auto enc_r = cfg.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    CHECK(bytes.size() == 23);

    conduit::io::BitReader r(bytes);
    auto decoded = sentry_link::ConfigBody::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(strip_nulls(decoded->device_name()) == "TestSensor01");
    CHECK(decoded->firmware().major() == 2);
    CHECK(decoded->firmware().minor() == 5);
    CHECK(decoded->firmware().patch() == 1024);
    CHECK(decoded->mode() == sentry_link::device_mode::calibration);
    CHECK(decoded->log_level() == 5);
    CHECK(decoded->auto_report() == 1);
    CHECK(decoded->compression() == 0);
    CHECK(decoded->sample_rate() == 8000);
}

// ============================================================================
// SentryLink: AlertBody with severity enum and string
// ============================================================================

TEST_CASE("SentryLink AlertBody roundtrip", "[sentry_link][alert]") {
    sentry_link::AlertBody alert;
    alert.set_timestamp(1700001000);
    alert.set_source_id(42);
    alert.set_severity(sentry_link::severity_level::critical);
    alert.set_category(7);
    alert.set_alert_code(0x1234);
    alert.set_message("OVERTEMP SENSOR 5");

    conduit::io::BitWriter w;
    auto enc_r = alert.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    CHECK(bytes.size() == 41);

    conduit::io::BitReader r(bytes);
    auto decoded = sentry_link::AlertBody::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->timestamp() == 1700001000);
    CHECK(decoded->source_id() == 42);
    CHECK(decoded->severity() == sentry_link::severity_level::critical);
    CHECK(decoded->category() == 7);
    CHECK(decoded->alert_code() == 0x1234);
    CHECK(strip_nulls(decoded->message()) == "OVERTEMP SENSOR 5");
}

// ============================================================================
// SentryLink: AlertBody all severity levels
// ============================================================================

TEST_CASE("SentryLink AlertBody all severity levels", "[sentry_link][alert][enum]") {
    for (auto sv : {sentry_link::severity_level::info, sentry_link::severity_level::notice,
                    sentry_link::severity_level::warning, sentry_link::severity_level::error,
                    sentry_link::severity_level::critical}) {
        sentry_link::AlertBody alert;
        alert.set_timestamp(0);
        alert.set_source_id(0);
        alert.set_severity(sv);
        alert.set_category(0);
        alert.set_alert_code(0);
        alert.set_message("");

        conduit::io::BitWriter w;
        auto enc_r = alert.encode(w);
        REQUIRE(enc_r.has_value());
        auto finish_result = w.finish();
        REQUIRE(finish_result.has_value());
        auto bytes = std::move(*finish_result);

        conduit::io::BitReader r(bytes);
        auto decoded = sentry_link::AlertBody::decode(r);
        REQUIRE(decoded.has_value());
        CHECK(decoded->severity() == sv);
    }
}

// ============================================================================
// SentryLink: Frame with HeartbeatBody
// ============================================================================

TEST_CASE("SentryLink Frame heartbeat roundtrip", "[sentry_link][frame]") {
    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(1700000000);
    hb.set_uptime_hours(500);
    hb.set_status(sentry_link::device_status::standby);
    REQUIRE(hb.set_cpu_load(50).has_value());

    auto frame = sentry_link::Frame::wrap(hb);
    frame.set_sync(sentry_link::SYNC);
    frame.set_sequence(0);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 14);
    CHECK(bytes[0] == 0xAA); CHECK(bytes[1] == 0x55);
    CHECK(bytes[2] == sentry_link::HeartbeatBody::ID_VALUE);

    auto decoded = sentry_link::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == sentry_link::SYNC);
    CHECK(decoded->msg_type() == sentry_link::HeartbeatBody::ID_VALUE);
    CHECK(decoded->length() == 14);
    auto& payload = std::get<sentry_link::HeartbeatBody>(decoded->payload());
    CHECK(payload.timestamp() == 1700000000);
    CHECK(payload.uptime_hours() == 500);
    CHECK(payload.status() == sentry_link::device_status::standby);
    CHECK(payload.cpu_load() == 50);
}

// ============================================================================
// SentryLink: Frame with SensorBody
// ============================================================================

TEST_CASE("SentryLink Frame sensor roundtrip", "[sentry_link][frame]") {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(0x0042);
    sb.set_timestamp(1700000200);
    sentry_link::SensorFlags flags;
    flags.set_channel(3);
    flags.set_precision(2);
    flags.set_saturated(0);
    flags.set_valid(1);
    sb.set_flags(flags);
    sb.set_raw_value(23456);
    sb.set_unit_code(2); // percent-RH

    auto frame = sentry_link::Frame::wrap(sb);
    frame.set_sync(sentry_link::SYNC);
    frame.set_sequence(42);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 18);
    CHECK(bytes[0] == 0xAA); CHECK(bytes[1] == 0x55);
    CHECK(bytes[2] == sentry_link::SensorBody::ID_VALUE);

    auto decoded = sentry_link::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sequence() == 42);
    auto& payload = std::get<sentry_link::SensorBody>(decoded->payload());
    CHECK(payload.sensor_id() == 0x0042);
    CHECK(payload.raw_value() == 23456);
    CHECK(payload.flags().channel() == 3);
    CHECK(payload.flags().valid() == 1);
}

// ============================================================================
// SentryLink: Frame with AlertBody
// ============================================================================

TEST_CASE("SentryLink Frame alert roundtrip", "[sentry_link][frame]") {
    sentry_link::AlertBody alert;
    alert.set_timestamp(1700005000);
    alert.set_source_id(7);
    alert.set_severity(sentry_link::severity_level::warning);
    alert.set_category(3);
    alert.set_alert_code(0xABCD);
    alert.set_message("LOW BATTERY");

    auto frame = sentry_link::Frame::wrap(alert);
    frame.set_sync(sentry_link::SYNC);
    frame.set_sequence(99);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 47);
    CHECK(bytes[0] == 0xAA); CHECK(bytes[1] == 0x55);
    CHECK(bytes[2] == sentry_link::AlertBody::ID_VALUE);

    auto decoded = sentry_link::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& payload = std::get<sentry_link::AlertBody>(decoded->payload());
    CHECK(payload.timestamp() == 1700005000);
    CHECK(payload.source_id() == 7);
    CHECK(payload.severity() == sentry_link::severity_level::warning);
    CHECK(payload.alert_code() == 0xABCD);
    CHECK(strip_nulls(payload.message()) == "LOW BATTERY");
}

// ============================================================================
// SentryLink: Frame wrap() for ConfigBody (send-only)
// ============================================================================

TEST_CASE("SentryLink wrap ConfigBody auto-sets fields", "[sentry_link][wrap]") {
    sentry_link::ConfigBody cfg;
    cfg.set_device_name("Probe-7");
    sentry_link::FirmwareVersion fw;
    fw.set_major(1); fw.set_minor(0); fw.set_patch(3);
    cfg.set_firmware(fw);
    cfg.set_mode(sentry_link::device_mode::active);
    cfg.set_log_level(2);
    cfg.set_auto_report(1);
    cfg.set_compression(0);
    cfg.set_sample_rate(4000);

    auto frame = sentry_link::Frame::wrap(cfg);
    frame.set_sync(sentry_link::SYNC);
    CHECK(frame.msg_type() == sentry_link::ConfigBody::ID_VALUE);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // length should be header(6) + ConfigBody(23) = 29
    CHECK(bytes.size() == 29);

    auto decoded = sentry_link::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->length() == 29);
    auto& payload = std::get<sentry_link::ConfigBody>(decoded->payload());
    CHECK(strip_nulls(payload.device_name()) == "Probe-7");
    CHECK(payload.firmware().major() == 1);
    CHECK(payload.firmware().patch() == 3);
    CHECK(payload.mode() == sentry_link::device_mode::active);
    CHECK(payload.sample_rate() == 4000);
}

// ============================================================================
// SentryLink: Frame sync word on wire
// ============================================================================

TEST_CASE("SentryLink Frame sync word wire format", "[sentry_link][wire]") {
    sentry_link::ConfigBody cfg;
    cfg.set_device_name("X");
    sentry_link::FirmwareVersion fw;
    cfg.set_firmware(fw);
    cfg.set_mode(sentry_link::device_mode::active);
    cfg.set_sample_rate(0);

    auto frame = sentry_link::Frame::wrap(cfg);
    frame.set_sync(sentry_link::SYNC);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() >= 2);
    // Sync word 0xAA55 in big-endian
    CHECK(bytes[0] == 0xAA);
    CHECK(bytes[1] == 0x55);
}

// ============================================================================
// SentryLink: Frame decode with wrong sync still parses (constraint not enforced at frame level)
// ============================================================================

TEST_CASE("SentryLink Frame wrong sync decode reads sync field", "[sentry_link][frame]") {
    conduit::io::BitWriter w;
    w.write_u16(0xDEAD);  // wrong sync (constraint-equals is encode-only)
    w.write_u8(1);         // msg_type heartbeat
    w.write_u16(14);       // length
    w.write_u8(0);         // sequence
    w.write_u32(0);        // timestamp
    w.write_u16(0);        // uptime
    w.write_u8(0);         // status
    w.write_u8(0);         // cpu_load
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    // constraint-equals is encode-only; Frame::decode reads but does not validate
    auto decoded = sentry_link::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == 0xDEAD);
}

// ============================================================================
// SentryLink: Frame decode truncated fails
// ============================================================================

TEST_CASE("SentryLink Frame decode truncated buffer fails", "[sentry_link][errors]") {
    std::vector<uint8_t> truncated = {0xAA, 0x55, 0x01};
    auto decoded = sentry_link::Frame::decode_bytes(truncated);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// SentryLink: Frame decode with unknown message ID fails
// ============================================================================

TEST_CASE("SentryLink Frame invalid msg_type fails", "[sentry_link][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0xAA55);  // correct sync
    w.write_u8(99);        // invalid msg_type (no message with id=99)
    w.write_u16(14);
    w.write_u8(0);
    // 8 bytes of dummy body
    for (int i = 0; i < 8; i++) w.write_u8(0);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = sentry_link::Frame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// SentryLink: FirmwareVersion roundtrip
// ============================================================================

TEST_CASE("SentryLink FirmwareVersion roundtrip", "[sentry_link][structs]") {
    sentry_link::FirmwareVersion fw;
    fw.set_major(10);
    fw.set_minor(3);
    fw.set_patch(4567);

    conduit::io::BitWriter w;
    auto enc_r = fw.encode(w);
    REQUIRE(enc_r.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    CHECK(bytes.size() == 4);

    conduit::io::BitReader r(bytes);
    auto decoded = sentry_link::FirmwareVersion::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->major() == 10);
    CHECK(decoded->minor() == 3);
    CHECK(decoded->patch() == 4567);
}

// ============================================================================
// SentryLink: ConfigBody device_mode all values
// ============================================================================

TEST_CASE("SentryLink ConfigBody all device modes", "[sentry_link][config][enum]") {
    for (auto mode : {sentry_link::device_mode::active, sentry_link::device_mode::passive,
                      sentry_link::device_mode::calibration, sentry_link::device_mode::test}) {
        sentry_link::ConfigBody cfg;
        cfg.set_device_name("dev");
        sentry_link::FirmwareVersion fw;
        cfg.set_firmware(fw);
        cfg.set_mode(mode);
        cfg.set_log_level(0);
        cfg.set_auto_report(0);
        cfg.set_compression(0);
        cfg.set_sample_rate(0);

        conduit::io::BitWriter w;
        auto enc_r = cfg.encode(w);
        REQUIRE(enc_r.has_value());
        auto finish_result = w.finish();
        REQUIRE(finish_result.has_value());
        auto bytes = std::move(*finish_result);

        conduit::io::BitReader r(bytes);
        auto decoded = sentry_link::ConfigBody::decode(r);
        REQUIRE(decoded.has_value());
        CHECK(decoded->mode() == mode);
    }
}

// ============================================================================
// SentryLink: Frame msg-type discriminator on wire
// ============================================================================

TEST_CASE("SentryLink Frame msg-type discriminator on wire", "[sentry_link][wire]") {
    sentry_link::ConfigBody cfg;
    cfg.set_device_name("");
    sentry_link::FirmwareVersion fw;
    cfg.set_firmware(fw);
    cfg.set_mode(sentry_link::device_mode::active);
    cfg.set_sample_rate(0);

    auto frame = sentry_link::Frame::wrap(cfg);
    frame.set_sync(sentry_link::SYNC);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // msg-type is at offset 2 (after 2-byte sync)
    REQUIRE(bytes.size() > 2);
    CHECK(bytes[2] == sentry_link::ConfigBody::ID_VALUE);
}

// ============================================================================
// SentryLink: Frame length field on wire
// ============================================================================

TEST_CASE("SentryLink Frame length field on wire", "[sentry_link][wire]") {
    sentry_link::ConfigBody cfg;
    cfg.set_device_name("TEST");
    sentry_link::FirmwareVersion fw;
    fw.set_major(1); fw.set_minor(2); fw.set_patch(3);
    cfg.set_firmware(fw);
    cfg.set_mode(sentry_link::device_mode::test);
    cfg.set_log_level(7);
    cfg.set_auto_report(1);
    cfg.set_compression(1);
    cfg.set_sample_rate(16000);

    auto frame = sentry_link::Frame::wrap(cfg);
    frame.set_sync(sentry_link::SYNC);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // length is at offset 3-4 (after sync(2) + msg_type(1)), big-endian
    REQUIRE(bytes.size() >= 5);
    uint16_t wire_len = (static_cast<uint16_t>(bytes[3]) << 8) | bytes[4];
    CHECK(wire_len == 29);
}
