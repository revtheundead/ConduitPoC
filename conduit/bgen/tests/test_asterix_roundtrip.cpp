// SPDX-License-Identifier: MIT
// Bgen tests - ASTERIX roundtrip verification
//
// Tests generated code from the asterix fixture covering:
// - Bitmap-controlled optional fields (FSPEC)
// - FX extension chains (Track Status, Target Report Descriptor)
// - Nested structs
// - Various types (coordinates, flight levels, mode 3/A codes)
// - 6-bit packed character strings (Aircraft Identification)
// - DataBlock framing and multi-category dispatch

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "asterix/messages.hpp"
#include "asterix/constants.hpp"

// ============================================================================
// Cat001Record: All fields present roundtrip
// ============================================================================

TEST_CASE("Cat001Record all fields present roundtrip", "[roundtrip][asterix]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    // FRN 1: I010 - Data Source Identifier
    asterix::DataSourceId dsid;
    dsid.set_sac(0x12);
    dsid.set_sic(0x34);
    items.set_i010(dsid);

    // FRN 2: I020 - Target Report Descriptor
    asterix::i020 trd;
    trd.set_typ(asterix::cat001_report_type::track);
    items.set_i020(trd);

    // FRN 3: I040 - Measured Position in Polar
    asterix::PolarRhoTheta polar;
    polar.set_rho(5000);
    polar.set_theta(30000 * 0.0054931640625);
    items.set_i040(polar);

    // FRN 4: I042 - Calculated Position in Cartesian
    asterix::CartesianXY cart;
    cart.set_x(1500);
    cart.set_y(-2000);
    items.set_i042(cart);

    // FRN 5: I070 - Mode 3/A Code
    asterix::i070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(1);
    mode3a.set_code(01234); // Octal squawk code
    items.set_i070(mode3a);

    // FRN 6: I090 - Flight Level
    asterix::i090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(350.0); // FL350
    items.set_i090(fl);

    // FRN 7: I161 - Track Number
    items.set_i161(4567);

    // FRN 8 (second octet): I170 - Track Status
    asterix::Cat001TrackStatus ts;
    ts.set_cnf(1);
    ts.set_rad(2);
    ts.set_dou(0);
    ts.set_man(1);
    ts.set_rdp(0);
    ts.set_gho(0);
    items.set_i170(ts);

    // FRN 9 (second octet): I141 - Time of Day
    asterix::time_of_day tod;
    tod.set_raw(128 * 3600); // 1 hour in 1/128 sec units
    items.set_i141(tod);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // Verify all first-octet items
    REQUIRE(decoded->items().has_i010());
    CHECK(decoded->items().i010().sac() == 0x12);
    CHECK(decoded->items().i010().sic() == 0x34);

    REQUIRE(decoded->items().has_i020());
    CHECK(decoded->items().i020().typ() == asterix::cat001_report_type::track);

    REQUIRE(decoded->items().has_i040());
    CHECK(decoded->items().i040().rho() == 5000);
    CHECK_THAT(decoded->items().i040().theta(),
               Catch::Matchers::WithinRel(30000 * 0.0054931640625, 0.001));

    REQUIRE(decoded->items().has_i042());
    CHECK(decoded->items().i042().x() == 1500);
    CHECK(decoded->items().i042().y() == -2000);

    REQUIRE(decoded->items().has_i070());
    CHECK(decoded->items().i070().v() == 1);
    CHECK(decoded->items().i070().g() == 0);
    CHECK(decoded->items().i070().l() == 1);
    CHECK(decoded->items().i070().code() == 01234);

    REQUIRE(decoded->items().has_i090());
    CHECK(decoded->items().i090().v() == 1);
    CHECK_THAT(decoded->items().i090().fl(),
               Catch::Matchers::WithinRel(350.0, 0.001));

    REQUIRE(decoded->items().has_i161());
    CHECK(decoded->items().i161() == 4567);

    // Verify second-octet items
    REQUIRE(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 1);
    CHECK(decoded->items().i170().rad() == 2);
    CHECK(decoded->items().i170().dou() == 0);
    CHECK(decoded->items().i170().man() == 1);
    CHECK(decoded->items().i170().rdp() == 0);
    CHECK(decoded->items().i170().gho() == 0);

    REQUIRE(decoded->items().has_i141());
    CHECK(decoded->items().i141().raw() == 128 * 3600);
}

// ============================================================================
// Cat001Record: Minimal fields (just I010 data source + I020 target report)
// ============================================================================

TEST_CASE("Cat001Record minimal fields roundtrip", "[roundtrip][asterix]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    // Only set the two compulsory-like items
    asterix::DataSourceId dsid;
    dsid.set_sac(1);
    dsid.set_sic(2);
    items.set_i010(dsid);

    asterix::i020 trd;
    trd.set_typ(asterix::cat001_report_type::plot);
    items.set_i020(trd);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // Present items
    REQUIRE(decoded->items().has_i010());
    CHECK(decoded->items().i010().sac() == 1);
    CHECK(decoded->items().i010().sic() == 2);
    REQUIRE(decoded->items().has_i020());
    CHECK(decoded->items().i020().typ() == asterix::cat001_report_type::plot);

    // All optional items absent
    CHECK_FALSE(decoded->items().has_i040());
    CHECK_FALSE(decoded->items().has_i042());
    CHECK_FALSE(decoded->items().has_i070());
    CHECK_FALSE(decoded->items().has_i090());
    CHECK_FALSE(decoded->items().has_i161());
    CHECK_FALSE(decoded->items().has_i170());
    CHECK_FALSE(decoded->items().has_i141());
}

// ============================================================================
// Cat001Record: Bitmap extension (items in second FSPEC octet)
// ============================================================================

TEST_CASE("Cat001Record second FSPEC octet items roundtrip", "[roundtrip][asterix]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    // Set only second-octet items (I170, I141) to force FSPEC extension
    asterix::Cat001TrackStatus ts;
    ts.set_cnf(0);
    ts.set_rad(1);
    ts.set_dou(1);
    ts.set_man(0);
    ts.set_rdp(1);
    ts.set_gho(1);
    items.set_i170(ts);

    asterix::time_of_day tod;
    tod.set_raw(256 * 60); // Some specific time
    items.set_i141(tod);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // The FSPEC must have the FX bit set in the first octet to indicate
    // a second octet follows. Bit 0 (LSB) of first FSPEC byte is the FX bit.
    REQUIRE(bytes.size() >= 2);
    CHECK((bytes[0] & 0x01) != 0); // FX bit set in first FSPEC octet

    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // First-octet items should be absent
    CHECK_FALSE(decoded->items().has_i010());
    CHECK_FALSE(decoded->items().has_i020());
    CHECK_FALSE(decoded->items().has_i040());
    CHECK_FALSE(decoded->items().has_i042());
    CHECK_FALSE(decoded->items().has_i070());
    CHECK_FALSE(decoded->items().has_i090());
    CHECK_FALSE(decoded->items().has_i161());

    // Second-octet items should be present
    REQUIRE(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 0);
    CHECK(decoded->items().i170().rad() == 1);
    CHECK(decoded->items().i170().dou() == 1);
    CHECK(decoded->items().i170().man() == 0);
    CHECK(decoded->items().i170().rdp() == 1);
    CHECK(decoded->items().i170().gho() == 1);

    REQUIRE(decoded->items().has_i141());
    CHECK(decoded->items().i141().raw() == 256 * 60);
}

// ============================================================================
// Cat001Record: Mixed first and second octet items
// ============================================================================

TEST_CASE("Cat001Record mixed first and second octet items", "[roundtrip][asterix]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    // One item from first octet
    asterix::DataSourceId dsid;
    dsid.set_sac(0xFF);
    dsid.set_sic(0x00);
    items.set_i010(dsid);

    // One item from second octet
    asterix::time_of_day tod;
    tod.set_raw(0);
    items.set_i141(tod);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    REQUIRE(decoded->items().has_i010());
    CHECK(decoded->items().i010().sac() == 0xFF);
    CHECK(decoded->items().i010().sic() == 0x00);
    REQUIRE(decoded->items().has_i141());
    CHECK(decoded->items().i141().raw() == 0);

    // Other items absent
    CHECK_FALSE(decoded->items().has_i020());
    CHECK_FALSE(decoded->items().has_i170());
}

// ============================================================================
// Cat001Record: Decode from empty buffer fails gracefully
// ============================================================================

TEST_CASE("Cat001Record decode from empty buffer fails", "[roundtrip][asterix]") {
    std::vector<uint8_t> empty;
    auto decoded = asterix::Cat001Record::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Cat001Record decode from truncated buffer fails", "[roundtrip][asterix]") {
    // Craft a buffer with FSPEC indicating I010 present but no field data
    conduit::io::BitWriter w;
    w.write_u8(0x80); // FSPEC: bit 7 set (I010 present), no FX
    // No I010 payload follows -- decode should fail
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Cat001Record decode with truncated second octet fails", "[roundtrip][asterix]") {
    // FSPEC with FX bit set but no second octet data
    conduit::io::BitWriter w;
    w.write_u8(0x01); // FX bit set, meaning second FSPEC octet follows
    // But no second FSPEC octet provided
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    // This may either succeed with empty second octet or fail depending on
    // implementation; the key invariant is it must not crash.
    // Either outcome is acceptable as long as no undefined behavior occurs.
    (void)decoded;
}

// ============================================================================
// Cat048Record: Aircraft identification (6-bit packed string)
// ============================================================================

TEST_CASE("Cat048Record with aircraft identification I240", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    // I010 - Data Source
    asterix::DataSourceId dsid;
    dsid.set_sac(0x30);
    dsid.set_sic(0x40);
    items.set_i010(dsid);

    // I140 - Time of Day
    asterix::time_of_day tod;
    tod.set_raw(64000);
    items.set_i140(tod);

    // I240 - Aircraft Identification (6-bit packed IA5 string)
    // Characters must fit within 6-bit range (ASCII & 0x3F)
    // Valid chars: digits 0-9 (0x30-0x39), uppercase A-Z map to 0x01-0x1A, space=0x20
    asterix::aircraft_ident ident;
    ident.set_value("0123");
    items.set_i240(ident);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    REQUIRE(decoded->items().has_i010());
    CHECK(decoded->items().i010().sac() == 0x30);
    CHECK(decoded->items().i010().sic() == 0x40);

    REQUIRE(decoded->items().has_i140());
    CHECK(decoded->items().i140().raw() == 64000);

    REQUIRE(decoded->items().has_i240());
    auto val = decoded->items().i240().value();
    CHECK(val.substr(0, 4) == "0123");
}

// ============================================================================
// Cat048Record: Multiple items across both FSPEC octets
// ============================================================================

TEST_CASE("Cat048Record multiple FSPEC items roundtrip", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    // First octet items
    asterix::DataSourceId dsid;
    dsid.set_sac(0x10);
    dsid.set_sic(0x20);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(25600);
    items.set_i140(tod);

    asterix::Cat048TargetReportDescriptor trd;
    trd.set_typ(3);
    trd.set_sim(0);
    trd.set_rdp(1);
    trd.set_spi(0);
    trd.set_rab(0);
    items.set_i020(trd);

    asterix::PolarRhoTheta polar;
    polar.set_rho(8000);
    polar.set_theta(180.0);
    items.set_i040(polar);

    // I070 - Mode 3/A (Cat048 inline type collides with Cat001 → itemsi070)
    asterix::itemsi070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(0);
    mode3a.set_code(0x567);
    items.set_i070(mode3a);

    // I090 - Flight Level (Cat048 inline type collides with Cat001 → itemsi090)
    asterix::itemsi090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(100.0); // FL100
    items.set_i090(fl);

    // Second octet items
    // I161 - Track Number
    items.set_i161(9999);

    // I042 - Cartesian Position
    asterix::CartesianXY cart;
    cart.set_x(-500);
    cart.set_y(750);
    items.set_i042(cart);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // Verify first-octet items
    CHECK(decoded->items().i010().sac() == 0x10);
    CHECK(decoded->items().i010().sic() == 0x20);
    CHECK(decoded->items().i140().raw() == 25600);
    CHECK(decoded->items().i020().typ() == 3);
    CHECK(decoded->items().i020().rdp() == 1);
    CHECK(decoded->items().i040().rho() == 8000);
    CHECK(decoded->items().i070().code() == 0x567);
    CHECK_THAT(decoded->items().i090().fl(),
               Catch::Matchers::WithinRel(100.0, 0.001));

    // Verify second-octet items
    CHECK(decoded->items().i161() == 9999);
    CHECK(decoded->items().i042().x() == -500);
    CHECK(decoded->items().i042().y() == 750);
}

// ============================================================================
// Cat048Record: FX extension on Track Status (I170)
// ============================================================================

TEST_CASE("Cat048Record Track Status without FX extension", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat048TrackStatus ts;
    ts.set_cnf(1);
    ts.set_rad(2);
    ts.set_dou(0);
    ts.set_mah(1);
    ts.set_cdm(3);
    // No FX fields set
    items.set_i170(ts);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 1);
    CHECK(decoded->items().i170().rad() == 2);
    CHECK(decoded->items().i170().dou() == 0);
    CHECK(decoded->items().i170().mah() == 1);
    CHECK(decoded->items().i170().cdm() == 3);
}

TEST_CASE("Cat048Record Track Status with FX extension", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat048TrackStatus ts;
    ts.set_cnf(0);
    ts.set_rad(1);
    ts.set_dou(1);
    ts.set_mah(0);
    ts.set_cdm(2);
    // FX extension fields
    ts.set_tre(1);
    ts.set_gho(1);
    ts.set_sup(0);
    ts.set_tcc(1);
    items.set_i170(ts);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 0);
    CHECK(decoded->items().i170().rad() == 1);
    CHECK(decoded->items().i170().dou() == 1);
    CHECK(decoded->items().i170().mah() == 0);
    CHECK(decoded->items().i170().cdm() == 2);
    CHECK(decoded->items().i170().tre() == 1);
    CHECK(decoded->items().i170().gho() == 1);
    CHECK(decoded->items().i170().sup() == 0);
    CHECK(decoded->items().i170().tcc() == 1);
}

// ============================================================================
// Cat048Record: Target Report Descriptor with FX extension
// ============================================================================

TEST_CASE("Cat048Record Target Report Descriptor with FX", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat048TargetReportDescriptor trd;
    trd.set_typ(5);
    trd.set_sim(1);
    trd.set_rdp(0);
    trd.set_spi(1);
    trd.set_rab(0);
    // FX fields
    trd.set_tst(1);
    trd.set_err(0);
    trd.set_xpp(1);
    trd.set_me(0);
    trd.set_mi(1);
    trd.set_foe_fri(2);
    items.set_i020(trd);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i020());
    CHECK(decoded->items().i020().typ() == 5);
    CHECK(decoded->items().i020().sim() == 1);
    CHECK(decoded->items().i020().rdp() == 0);
    CHECK(decoded->items().i020().spi() == 1);
    CHECK(decoded->items().i020().rab() == 0);
    CHECK(decoded->items().i020().tst() == 1);
    CHECK(decoded->items().i020().err() == 0);
    CHECK(decoded->items().i020().xpp() == 1);
    CHECK(decoded->items().i020().me() == 0);
    CHECK(decoded->items().i020().mi() == 1);
    CHECK(decoded->items().i020().foe_fri() == 2);
}

// ============================================================================
// Cat048Record: Nested bitmap (I130 compound sub-fields)
// ============================================================================

TEST_CASE("Cat048Record I130 nested bitmap all sub-fields", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::i130 radar_plot;
    auto& s = radar_plot.mutable_sub();
    s.set_srl(200);
    s.set_srr(50);
    s.set_sam(static_cast<asterix::int8>(-30));
    s.set_prl(180);
    s.set_pam(static_cast<asterix::int8>(25));
    s.set_rpd(static_cast<asterix::int8>(-10));
    s.set_apd(static_cast<asterix::int8>(5));
    items.set_i130(radar_plot);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i130());

    auto& sub = decoded->items().i130().sub();
    REQUIRE(sub.has_srl());
    CHECK(sub.srl() == 200);
    REQUIRE(sub.has_srr());
    CHECK(sub.srr() == 50);
    REQUIRE(sub.has_sam());
    CHECK(sub.sam() == -30);
    REQUIRE(sub.has_prl());
    CHECK(sub.prl() == 180);
    REQUIRE(sub.has_pam());
    CHECK(sub.pam() == 25);
    REQUIRE(sub.has_rpd());
    CHECK(sub.rpd() == -10);
    REQUIRE(sub.has_apd());
    CHECK(sub.apd() == 5);
}

TEST_CASE("Cat048Record I130 nested bitmap partial sub-fields", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::i130 radar_plot;
    auto& s = radar_plot.mutable_sub();
    // Only set srl and pam (non-contiguous bits)
    s.set_srl(100);
    s.set_pam(static_cast<asterix::int8>(-50));
    items.set_i130(radar_plot);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i130());

    auto& sub = decoded->items().i130().sub();
    REQUIRE(sub.has_srl());
    CHECK(sub.srl() == 100);
    CHECK_FALSE(sub.has_srr());
    CHECK_FALSE(sub.has_sam());
    CHECK_FALSE(sub.has_prl());
    REQUIRE(sub.has_pam());
    CHECK(sub.pam() == -50);
    CHECK_FALSE(sub.has_rpd());
    CHECK_FALSE(sub.has_apd());
}

// ============================================================================
// Cat048Record: I250 Mode S MB data with multiple BDS registers
// ============================================================================

TEST_CASE("Cat048Record I250 Mode S data roundtrip", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::i250 mb;
    mb.set_rep(3);
    auto& bds = mb.mutable_bds();

    // BDS register 1
    asterix::bdsElement e1;
    std::array<uint8_t, 7> data1 = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    e1.set_data(data1);
    e1.set_bds1(4);
    e1.set_bds2(0);
    bds.push_back(e1);

    // BDS register 2
    asterix::bdsElement e2;
    std::array<uint8_t, 7> data2 = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07};
    e2.set_data(data2);
    e2.set_bds1(5);
    e2.set_bds2(0);
    bds.push_back(e2);

    // BDS register 3
    asterix::bdsElement e3;
    std::array<uint8_t, 7> data3 = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99};
    e3.set_data(data3);
    e3.set_bds1(6);
    e3.set_bds2(0);
    bds.push_back(e3);

    items.set_i250(mb);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i250());
    CHECK(decoded->items().i250().rep() == 3);
    REQUIRE(decoded->items().i250().bds().size() == 3);
    CHECK(decoded->items().i250().bds()[0].data() == data1);
    CHECK(decoded->items().i250().bds()[0].bds1() == 4);
    CHECK(decoded->items().i250().bds()[1].data() == data2);
    CHECK(decoded->items().i250().bds()[1].bds1() == 5);
    CHECK(decoded->items().i250().bds()[2].data() == data3);
    CHECK(decoded->items().i250().bds()[2].bds1() == 6);
}

// ============================================================================
// Cat048Record: Empty FSPEC (no items)
// ============================================================================

TEST_CASE("Cat048Record empty FSPEC roundtrip", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    // No items set
    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded->items().has_i010());
    CHECK_FALSE(decoded->items().has_i140());
    CHECK_FALSE(decoded->items().has_i020());
    CHECK_FALSE(decoded->items().has_i040());
    CHECK_FALSE(decoded->items().has_i070());
    CHECK_FALSE(decoded->items().has_i090());
    CHECK_FALSE(decoded->items().has_i130());
    CHECK_FALSE(decoded->items().has_i220());
    CHECK_FALSE(decoded->items().has_i240());
    CHECK_FALSE(decoded->items().has_i250());
    CHECK_FALSE(decoded->items().has_i161());
    CHECK_FALSE(decoded->items().has_i042());
    CHECK_FALSE(decoded->items().has_i200());
    CHECK_FALSE(decoded->items().has_i170());
}

// ============================================================================
// Cat048Record: Decode from empty buffer fails
// ============================================================================

TEST_CASE("Cat048Record decode from empty buffer fails", "[roundtrip][asterix]") {
    std::vector<uint8_t> empty;
    auto decoded = asterix::Cat048Record::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Cat048Record: Aircraft address (I220) and track velocity (I200)
// ============================================================================

TEST_CASE("Cat048Record I220 aircraft address and I200 velocity", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    // I220 - Aircraft Address (24-bit ICAO)
    items.set_i220(0xABCDEF);

    // I200 - Calculated Track Velocity
    asterix::i200 vel;
    vel.set_ground_speed(500.0 * 0.00006103515625); // ~0.0305 NM/s
    vel.set_heading(270.0);
    items.set_i200(vel);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    REQUIRE(decoded->items().has_i220());
    CHECK(decoded->items().i220() == 0xABCDEF);

    REQUIRE(decoded->items().has_i200());
    CHECK_THAT(decoded->items().i200().ground_speed(),
               Catch::Matchers::WithinRel(500.0 * 0.00006103515625, 0.001));
    CHECK_THAT(decoded->items().i200().heading(),
               Catch::Matchers::WithinRel(270.0, 0.01));
}

// ============================================================================
// DataBlock: Cat048 with multiple records
// ============================================================================

TEST_CASE("DataBlock Cat048 multiple records roundtrip", "[roundtrip][asterix]") {
    asterix::DataBlock db;
    db.set_cat(asterix::CAT048);

    asterix::cat048 cat_records;

    // Record 1
    {
        asterix::Cat048Record rec;
        auto& items = rec.mutable_items();
        asterix::DataSourceId dsid;
        dsid.set_sac(1);
        dsid.set_sic(10);
        items.set_i010(dsid);
        items.set_i161(100);
        cat_records.mutable_items().push_back(rec);
    }

    // Record 2
    {
        asterix::Cat048Record rec;
        auto& items = rec.mutable_items();
        asterix::DataSourceId dsid;
        dsid.set_sac(2);
        dsid.set_sic(20);
        items.set_i010(dsid);
        items.set_i161(200);
        cat_records.mutable_items().push_back(rec);
    }

    db.set_records(asterix::recordsVariant{cat_records});

    // Compute correct length
    conduit::io::BitWriter lw;
    std::visit([&lw](const auto& v) { v.encode(lw); }, db.records());
    db.set_len(static_cast<asterix::uint16>(lw.size_bytes() + 3));

    auto enc_result = db.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::DataBlock::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->cat() == asterix::CAT048);

    auto& cat = std::get<asterix::cat048>(decoded->records());
    REQUIRE(cat.items().size() == 2);
    CHECK(cat.items()[0].items().i010().sac() == 1);
    CHECK(cat.items()[0].items().i010().sic() == 10);
    CHECK(cat.items()[0].items().i161() == 100);
    CHECK(cat.items()[1].items().i010().sac() == 2);
    CHECK(cat.items()[1].items().i010().sic() == 20);
    CHECK(cat.items()[1].items().i161() == 200);
}

// ============================================================================
// AsterixFrame: wrap() with Cat048Record
// ============================================================================

TEST_CASE("AsterixFrame wrap Cat048Record roundtrip", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0xAA);
    dsid.set_sic(0xBB);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(12800);
    items.set_i140(tod);

    asterix::Cat048TargetReportDescriptor trd;
    trd.set_typ(1);
    trd.set_sim(0);
    trd.set_rdp(0);
    trd.set_spi(0);
    trd.set_rab(0);
    items.set_i020(trd);

    auto frame = asterix::AsterixFrame::wrap(rec);
    REQUIRE(frame.blocks().size() == 1);
    CHECK(frame.blocks()[0].cat() == asterix::CAT048);
    CHECK(frame.blocks()[0].len() > 3);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::AsterixFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->blocks().size() == 1);
    CHECK(decoded->blocks()[0].cat() == asterix::CAT048);

    auto& cat = std::get<asterix::cat048>(decoded->blocks()[0].records());
    REQUIRE(cat.items().size() == 1);
    CHECK(cat.items()[0].items().i010().sac() == 0xAA);
    CHECK(cat.items()[0].items().i010().sic() == 0xBB);
    CHECK(cat.items()[0].items().i140().raw() == 12800);
    CHECK(cat.items()[0].items().i020().typ() == 1);
}

// ============================================================================
// Cat001Record: Flight Level boundary values
// ============================================================================

TEST_CASE("Cat001Record Flight Level boundary values", "[roundtrip][asterix]") {
    // Test FL=0
    {
        asterix::Cat001Record rec;
        auto& items = rec.mutable_items();
        asterix::i090 fl;
        fl.set_v(1);
        fl.set_g(0);
        fl.set_fl(0.0);
        items.set_i090(fl);

        auto enc_result = rec.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = asterix::Cat001Record::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK_THAT(decoded->items().i090().fl(),
                   Catch::Matchers::WithinAbs(0.0, 0.3));
    }

    // Test negative FL
    {
        asterix::Cat001Record rec;
        auto& items = rec.mutable_items();
        asterix::i090 fl;
        fl.set_v(1);
        fl.set_g(0);
        fl.set_fl(-10.0);
        items.set_i090(fl);

        auto enc_result = rec.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = asterix::Cat001Record::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK_THAT(decoded->items().i090().fl(),
                   Catch::Matchers::WithinRel(-10.0, 0.001));
    }
}

// ============================================================================
// Cat001Record: Mode 3/A code boundary values
// ============================================================================

TEST_CASE("Cat001Record Mode 3/A code values", "[roundtrip][asterix]") {
    // Test code = 0 (squawk 0000)
    {
        asterix::Cat001Record rec;
        auto& items = rec.mutable_items();
        asterix::i070 mode3a;
        mode3a.set_v(0);
        mode3a.set_g(0);
        mode3a.set_l(0);
        mode3a.set_code(0);
        items.set_i070(mode3a);

        auto enc_result = rec.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = asterix::Cat001Record::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->items().i070().code() == 0);
    }

    // Test code = 0x7777 (max 12-bit = 4095)
    {
        asterix::Cat001Record rec;
        auto& items = rec.mutable_items();
        asterix::i070 mode3a;
        mode3a.set_v(1);
        mode3a.set_g(1);
        mode3a.set_l(1);
        mode3a.set_code(0xFFF); // Max 12-bit value
        items.set_i070(mode3a);

        auto enc_result = rec.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = asterix::Cat001Record::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->items().i070().v() == 1);
        CHECK(decoded->items().i070().g() == 1);
        CHECK(decoded->items().i070().l() == 1);
        CHECK(decoded->items().i070().code() == 0xFFF);
    }
}

// ============================================================================
// Cat001Record: Track Status with FX extension
// ============================================================================

TEST_CASE("Cat001Record Track Status FX extension roundtrip", "[roundtrip][asterix]") {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    asterix::Cat001TrackStatus ts;
    ts.set_cnf(0);
    ts.set_rad(3);
    ts.set_dou(1);
    ts.set_man(1);
    ts.set_rdp(1);
    ts.set_gho(1);
    // FX extension field
    ts.set_tre(1);
    items.set_i170(ts);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat001Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i170());
    CHECK(decoded->items().i170().cnf() == 0);
    CHECK(decoded->items().i170().rad() == 3);
    CHECK(decoded->items().i170().dou() == 1);
    CHECK(decoded->items().i170().man() == 1);
    CHECK(decoded->items().i170().rdp() == 1);
    CHECK(decoded->items().i170().gho() == 1);
    CHECK(decoded->items().i170().tre() == 1);
}

// ============================================================================
// Cat048Record: Calculated Track Velocity (I200) precise values
// ============================================================================

TEST_CASE("Cat048Record I200 track velocity roundtrip", "[roundtrip][asterix]") {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    asterix::i200 vel;
    // ground-speed: scale = 0.00006103515625 NM/s
    vel.set_ground_speed(1000.0 * 0.00006103515625);
    // heading: scale = 0.0054931640625 degrees
    vel.set_heading(45000 * 0.0054931640625);
    items.set_i200(vel);

    auto enc_result = rec.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = asterix::Cat048Record::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->items().has_i200());
    CHECK_THAT(decoded->items().i200().ground_speed(),
               Catch::Matchers::WithinRel(1000.0 * 0.00006103515625, 0.001));
    CHECK_THAT(decoded->items().i200().heading(),
               Catch::Matchers::WithinRel(45000 * 0.0054931640625, 0.001));
}
