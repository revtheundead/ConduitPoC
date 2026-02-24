// SPDX-License-Identifier: MIT
// ============================================================================
// ASTERIX Example — Encode/Decode Roundtrip
//
// Demonstrates Conduit's generated ASTERIX protocol types:
//   - All 3 ASTERIX categories: CAT001, CAT048, CAT253
//   - Bitmap-controlled optional fields (FSPEC)
//   - FX extension chains (Cat001TrackStatus, Cat048TargetReportDescriptor)
//   - Nested bitmaps (Cat048 I130 compound sub-fields)
//   - Discriminated unions / choices (DataBlock category dispatch)
//   - All 5 Cat253 I100 formats (FormatA through FormatE)
//   - Scaled types (WGS-84 coordinates, time-of-day, flight level)
//   - 6-bit packed character strings (aircraft identification)
//   - Count-from arrays (Mode S MB data, Format C records)
//   - Length-prefixed strings (Cat253 I110 text annotation)
//   - Variable-length byte fields (RE/SP expansion fields)
//   - AsterixFrame multi-category DataBlock container
//
// Architecture:
//   - Each record type is built, encoded to bytes, decoded back, and verified
//   - Field-by-field verification ensures encode/decode roundtrip fidelity
// ============================================================================

#include <asterix/messages.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <variant>

// ============================================================================
// Helpers
// ============================================================================

template<typename... Args>
static void log(const char* tag, [[maybe_unused]] const char* fmt, Args... args) {
    std::printf("[%-8s] ", tag);
    if constexpr (sizeof...(args) == 0) {
        std::fputs(fmt, stdout);
    } else {
        std::printf(fmt, args...);
    }
    std::printf("\n");
    std::fflush(stdout);
}

static std::string strip_nulls(const std::string& s) {
    auto pos = s.find('\0');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

// ============================================================================
// Roundtrip helper — encode to bytes and decode back
// ============================================================================

template<typename T>
static conduit::Result<T> roundtrip(const T& msg) {
    auto bytes = msg.encode_bytes();
    if (!bytes) return std::unexpected(bytes.error());
    return T::decode_bytes(*bytes);
}

// ============================================================================
// Build Cat001Record — exercises FSPEC bitmap, FX chain, all item types
// ============================================================================

static asterix::Cat001Record make_cat001_full() {
    asterix::Cat001Record rec;
    auto& items = rec.mutable_items();

    // FRN 1: Data Source Identifier (SAC/SIC)
    asterix::DataSourceId dsid;
    dsid.set_sac(0x12);
    dsid.set_sic(0x34);
    items.set_i010(dsid);

    // FRN 2: Target Report Descriptor
    asterix::Cat001Record_items_i020 trd;
    trd.set_typ(asterix::cat001_report_type::track);
    items.set_i020(trd);

    // FRN 3: Measured Position in Polar Coordinates (physical values)
    asterix::PolarRhoTheta polar;
    polar.set_rho(25000);                          // 25000 meters
    polar.set_theta(45.0);                         // 45 degrees azimuth
    items.set_i040(polar);

    // FRN 4: Calculated Position in Cartesian Coordinates
    asterix::CartesianXY cart;
    cart.set_x(17678);                             // ~17.7 km
    cart.set_y(-17678);                            // negative Y
    items.set_i042(cart);

    // FRN 5: Mode 3/A Code (octal squawk code with validation flags)
    asterix::Cat001Record_items_i070 mode3a;
    mode3a.set_v(1);                               // validated
    mode3a.set_g(0);                               // not garbled
    mode3a.set_l(1);                               // local
    // reserved bit auto-handled
    mode3a.set_code(01234);                        // octal squawk 1234
    items.set_i070(mode3a);

    // FRN 6: Flight Level (signed 12-bit, scale=0.25 FL)
    asterix::Cat001Record_items_i090 fl;
    fl.set_v(1);                                   // validated
    fl.set_g(0);
    // reserved bits auto-handled
    fl.set_fl(350.0);                              // FL350
    items.set_i090(fl);

    // FRN 7: Track Number
    items.set_i161(4567);

    // --- Second FSPEC octet (extension bit set) ---

    // FRN 8: Track Status with FX extension chain
    asterix::Cat001TrackStatus ts;
    ts.set_cnf(1);                                 // confirmed track
    ts.set_rad(2);                                 // SSR/ModeS
    ts.set_dou(0);                                 // not doubtful
    ts.set_man(1);                                 // maneuvering
    ts.set_rdp(0);                                 // RDP chain 1
    ts.set_gho(0);                                 // not ghost
    // FX extension:
    ts.set_tre(1);                                 // track end
    items.set_i170(ts);

    // FRN 9: Time of Day (24-bit, scale = 1/128 second)
    asterix::time_of_day tod;
    // 12:00:00 UTC = 43200 seconds × 128 = 5529600
    tod.set_raw(5529600);
    items.set_i141(tod);

    return rec;
}

// ============================================================================
// Build Cat048Record — FX chains, nested bitmap, 6-bit packed strings, arrays
// ============================================================================

static asterix::Cat048Record make_cat048_full() {
    asterix::Cat048Record rec;
    auto& items = rec.mutable_items();

    // FRN 1: Data Source Identifier
    asterix::DataSourceId dsid;
    dsid.set_sac(0x30);
    dsid.set_sic(0x40);
    items.set_i010(dsid);

    // FRN 2: Time of Day
    asterix::time_of_day tod;
    // 08:30:00 UTC = 30600 seconds × 128 = 3916800
    tod.set_raw(3916800);
    items.set_i140(tod);

    // FRN 3: Target Report Descriptor with FX extension
    asterix::Cat048TargetReportDescriptor trd;
    trd.set_typ(3);                                // SSR + PSR
    trd.set_sim(0);                                // real target
    trd.set_rdp(1);                                // RDP chain 2
    trd.set_spi(1);                                // SPI present
    trd.set_rab(0);                                // from radar
    // FX extension fields:
    trd.set_tst(0);                                // real target
    trd.set_err(0);                                // no error
    trd.set_xpp(1);                                // X-Pulse present
    trd.set_me(0);                                 // no military emergency
    trd.set_mi(0);                                 // no military ident
    trd.set_foe_fri(1);                            // friendly
    items.set_i020(trd);

    // FRN 4: Measured Position in Slant Polar (physical values)
    asterix::PolarRhoTheta polar;
    polar.set_rho(60000);                          // 60 km range
    polar.set_theta(120.0);                        // 120 degrees azimuth
    items.set_i040(polar);

    // FRN 5: Mode 3/A Code
    // Cat048's i070 is fully qualified to disambiguate from Cat001's i070
    asterix::Cat048Record_items_i070 mode3a;
    mode3a.set_v(1);
    mode3a.set_g(0);
    mode3a.set_l(0);
    mode3a.set_code(07700);                        // emergency squawk
    items.set_i070(mode3a);

    // FRN 6: Flight Level (14-bit signed, scale=0.25)
    // Cat048's i090 is fully qualified to disambiguate from Cat001's i090
    asterix::Cat048Record_items_i090 fl;
    fl.set_v(1);
    fl.set_g(0);
    fl.set_fl(380.0);                              // FL380
    items.set_i090(fl);

    // FRN 7: Radar Plot Characteristics — nested bitmap
    asterix::Cat048Record_items_i130 radar_plot;
    auto& sub = radar_plot.mutable_sub();
    sub.set_srl(200);                              // SSR range loss
    sub.set_srr(15);                               // SSR azimuth
    sub.set_sam(static_cast<asterix::int8>(-30));  // SSR amplitude (signed)
    sub.set_prl(180);                              // PSR range loss
    sub.set_pam(static_cast<asterix::int8>(42));   // PSR amplitude
    sub.set_rpd(static_cast<asterix::int8>(-5));   // range plot difference
    sub.set_apd(static_cast<asterix::int8>(3));    // azimuth plot difference
    items.set_i130(radar_plot);

    // --- Second FSPEC octet ---

    // FRN 8: Aircraft Address (24-bit ICAO hex)
    items.set_i220(static_cast<asterix::aircraft_address>(0xABCDEF));

    // FRN 9: Aircraft Identification (8 chars × 6 bits = 48 bits packed)
    asterix::aircraft_ident ident;
    ident.set_value("BAW256  ");                   // British Airways 256
    items.set_i240(ident);

    // FRN 10: Mode S MB Data (count-from array)
    asterix::Cat048Record_items_i250 mb;
    mb.set_rep(2);                                 // 2 BDS registers
    auto& bds = mb.mutable_bds();

    asterix::Cat048Record_items_i250_bdsElement e1;
    // 56-bit MB data packed into uint64_t (7 bytes, MSB-first)
    e1.set_data(0x10203040506070ULL);
    e1.set_bds1(4);                                // BDS 4,0
    e1.set_bds2(0);
    bds.push_back(e1);

    asterix::Cat048Record_items_i250_bdsElement e2;
    e2.set_data(0xA1B2C3D4E5F607ULL);
    e2.set_bds1(5);                                // BDS 5,0
    e2.set_bds2(0);
    bds.push_back(e2);

    items.set_i250(mb);

    // FRN 11: Track Number
    items.set_i161(9999);

    // FRN 12: Calculated Position in Cartesian
    asterix::CartesianXY cart;
    cart.set_x(-5000);
    cart.set_y(12000);
    items.set_i042(cart);

    // FRN 13: Calculated Track Velocity (scaled physical values)
    asterix::Cat048Record_items_i200 vel;
    vel.set_ground_speed(0.027);                   // ~0.027 NM/s ≈ 97 knots
    vel.set_heading(270.0);                        // 270 degrees
    items.set_i200(vel);

    // FRN 14: Track Status with FX
    asterix::Cat048TrackStatus tst;
    tst.set_cnf(0);                                // confirmed
    tst.set_rad(1);                                // PSR only
    tst.set_dou(0);
    tst.set_mah(0);
    tst.set_cdm(2);                                // climbing
    // FX extension:
    tst.set_tre(1);                                // last message
    tst.set_gho(0);                                // not ghost
    tst.set_sup(1);                                // updated by association
    tst.set_tcc(0);                                // radar plane
    items.set_i170(tst);

    return rec;
}

// ============================================================================
// Build Cat253Record — all 5 I100 format variants
// ============================================================================

// Format A: Simple fixed payload (selector 0-255)
static asterix::Cat253Record make_cat253_format_a() {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    // Compulsory items I010-I050
    asterix::DataSourceId dsid;
    dsid.set_sac(0x01); dsid.set_sic(0x02);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(128 * 3600);                       // 1 hour = 3600 sec
    items.set_i020(tod);

    items.set_i030(static_cast<asterix::uint16>(1000));

    asterix::Cat253I040 mtype;
    mtype.set_msg_type(asterix::cat253_msg_type::extended_data);
    items.set_i040(mtype);

    items.set_i050(static_cast<asterix::uint8>(5));

    // I060: Status (compulsory for type 8)
    asterix::Cat253I060 status;
    status.set_operational(1);
    status.set_degraded(0);
    status.set_maintenance(0);
    status.set_error_code(0);
    items.set_i060(status);

    // I070: Sequence (compulsory for type 8)
    asterix::Cat253I070 seq;
    seq.set_sequence(0x0001);
    seq.set_fragment(1);
    seq.set_total_fragments(1);
    items.set_i070(seq);

    // I080: Structure selector → Format A (range 0-255)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(42);
    items.set_i080(ctrl);

    // I100: Format A — fixed 16-byte payload
    asterix::Cat253I100FormatA fmt_a;
    fmt_a.set_data_type(7);
    std::array<uint8_t, 16> payload{};
    for (size_t i = 0; i < 16; i++) payload[i] = static_cast<uint8_t>(i * 17);
    fmt_a.set_payload(payload);
    items.set_i100(asterix::Cat253Record_items_i100Variant{fmt_a});

    return rec;
}

// Format B: Variable-length payload (selector 256-511)
static asterix::Cat253Record make_cat253_format_b() {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0x03); dsid.set_sic(0x04);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(128 * 7200);                       // 2 hours
    items.set_i020(tod);

    items.set_i030(static_cast<asterix::uint16>(2000));

    asterix::Cat253I040 mtype;
    mtype.set_msg_type(asterix::cat253_msg_type::data);
    items.set_i040(mtype);

    items.set_i050(static_cast<asterix::uint8>(3));

    // I080: selector → Format B (256-511)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(300);
    items.set_i080(ctrl);

    // I100: Format B — variable length with header
    asterix::Cat253I100FormatB fmt_b;
    fmt_b.set_version(2);
    fmt_b.set_flags(0xAB);
    fmt_b.set_payload_length(8);
    fmt_b.set_payload({0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE});
    items.set_i100(asterix::Cat253Record_items_i100Variant{fmt_b});

    return rec;
}

// Format C: Structured records array (selector 512-1023)
static asterix::Cat253Record make_cat253_format_c() {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0x05); dsid.set_sic(0x06);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(128 * 10800);                      // 3 hours
    items.set_i020(tod);

    items.set_i030(static_cast<asterix::uint16>(3000));

    asterix::Cat253I040 mtype;
    mtype.set_msg_type(asterix::cat253_msg_type::data);
    items.set_i040(mtype);

    items.set_i050(static_cast<asterix::uint8>(7));

    // I080: selector → Format C (512-1023)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(750);
    items.set_i080(ctrl);

    // I090: Priority and flags
    asterix::Cat253I090 pf;
    pf.set_priority(15);
    pf.set_urgent(1);
    pf.set_ack_required(1);
    pf.set_encrypted(0);
    pf.set_compressed(1);
    items.set_i090(pf);

    // I100: Format C — structured records with count-from array
    asterix::Cat253I100FormatC fmt_c;
    fmt_c.set_record_type(3);
    fmt_c.set_record_count(3);

    auto& recs = fmt_c.mutable_records();

    asterix::Cat253I100FormatC_recordsElement r1;
    r1.set_id(100);
    r1.set_value(0xDEADBEEF);
    asterix::time_of_day t1; t1.set_raw(25600);
    r1.set_timestamp(t1);
    recs.push_back(r1);

    asterix::Cat253I100FormatC_recordsElement r2;
    r2.set_id(200);
    r2.set_value(0xCAFEBABE);
    asterix::time_of_day t2; t2.set_raw(51200);
    r2.set_timestamp(t2);
    recs.push_back(r2);

    asterix::Cat253I100FormatC_recordsElement r3;
    r3.set_id(300);
    r3.set_value(0x12345678);
    asterix::time_of_day t3; t3.set_raw(76800);
    r3.set_timestamp(t3);
    recs.push_back(r3);

    items.set_i100(asterix::Cat253Record_items_i100Variant{fmt_c});

    // I110: Text annotation (length-prefixed string)
    asterix::Cat253I110 text;
    text.set_text("FORMAT-C TEST");
    items.set_i110(text);

    return rec;
}

// Format D: Position data (selector 1024-2047)
static asterix::Cat253Record make_cat253_format_d() {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0x07); dsid.set_sic(0x08);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(128 * 14400);                      // 4 hours
    items.set_i020(tod);

    items.set_i030(static_cast<asterix::uint16>(4000));

    asterix::Cat253I040 mtype;
    mtype.set_msg_type(asterix::cat253_msg_type::data);
    items.set_i040(mtype);

    items.set_i050(static_cast<asterix::uint8>(1));

    // I080: selector → Format D (1024-2047)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(1500);
    items.set_i080(ctrl);

    // I100: Format D — geographic position data with scaled fields
    asterix::Cat253I100FormatD fmt_d;

    // WGS-84 fine coordinates (32-bit signed, scale = 180/2^31)
    // 51.4775°N (London Heathrow) → raw ≈ 51.4775 / 8.381903e-8 ≈ 614,283,000
    asterix::wgs84_fine lat;
    lat.set_raw(614283000);
    fmt_d.set_latitude(lat);

    // -0.4614°W → raw ≈ -0.4614 / 8.381903e-8 ≈ -5,505,000
    asterix::wgs84_fine lon;
    lon.set_raw(-5505000);
    fmt_d.set_longitude(lon);

    // Altitude: 35000 feet (scale=0.25 → raw = 140000)
    fmt_d.set_altitude(35000.0);

    // Ground speed: 480 knots (scale=0.1 → raw = 4800)
    fmt_d.set_ground_speed(480.0);

    // Heading: 90 degrees (scale=0.0054931640625 → raw ≈ 16384)
    fmt_d.set_heading(90.0);

    items.set_i100(asterix::Cat253Record_items_i100Variant{fmt_d});

    // I120: Reference data
    asterix::Cat253I120 ref;
    ref.set_ref_type(5);
    ref.set_ref_id(0xABCD1234);
    items.set_i120(ref);

    return rec;
}

// Format E: Extended data with nested bitmap (selector 2048-4095)
static asterix::Cat253Record make_cat253_format_e() {
    asterix::Cat253Record rec;
    auto& items = rec.mutable_items();

    asterix::DataSourceId dsid;
    dsid.set_sac(0x09); dsid.set_sic(0x0A);
    items.set_i010(dsid);

    asterix::time_of_day tod;
    tod.set_raw(128 * 18000);                      // 5 hours
    items.set_i020(tod);

    items.set_i030(static_cast<asterix::uint16>(5000));

    asterix::Cat253I040 mtype;
    mtype.set_msg_type(asterix::cat253_msg_type::extended_data);
    items.set_i040(mtype);

    items.set_i050(static_cast<asterix::uint8>(9));

    // I060: Status
    asterix::Cat253I060 status;
    status.set_operational(1);
    status.set_degraded(1);                        // degraded mode
    status.set_maintenance(0);
    status.set_error_code(7);
    items.set_i060(status);

    // I070: Sequence
    asterix::Cat253I070 seq;
    seq.set_sequence(0xFFFF);                      // max sequence
    seq.set_fragment(5);
    seq.set_total_fragments(10);
    items.set_i070(seq);

    // I080: selector → Format E (2048-4095)
    asterix::Cat253I080 ctrl;
    ctrl.set_structure_selector(3000);
    items.set_i080(ctrl);

    // I090: Priority
    asterix::Cat253I090 pf;
    pf.set_priority(0);
    pf.set_urgent(0);
    pf.set_ack_required(0);
    pf.set_encrypted(1);
    pf.set_compressed(0);
    items.set_i090(pf);

    // I100: Format E — extended with nested presence bitmap
    asterix::Cat253I100FormatE fmt_e;
    fmt_e.set_format_version(4);
    fmt_e.set_total_length(100);                   // informational

    auto& sub = fmt_e.mutable_sub_items();

    // Sub-item: source-id (bit 7)
    asterix::DataSourceId src;
    src.set_sac(0xBB);
    src.set_sic(0xCC);
    sub.set_source_id(src);

    // Sub-item: callsign (bit 6) — 8-char IA5 space-padded
    asterix::callsign cs;
    cs.set_value("TEST1234");
    sub.set_callsign(cs);

    // Sub-item: aircraft-addr (bit 5) — 24-bit ICAO hex
    sub.set_aircraft_addr(static_cast<asterix::aircraft_address>(0x4CA123));

    // Sub-item: position (bit 4) — WGS-84 fine lat/lon
    asterix::Cat253I100FormatE_sub_items_position pos;
    asterix::wgs84_fine plat, plon;
    plat.set_raw(500000000);                       // ~41.9°N
    plon.set_raw(-100000000);                      // ~-8.38°W
    pos.set_lat(plat);
    pos.set_lon(plon);
    sub.set_position(pos);

    // Sub-item: flight-level (bit 3) — 16-bit signed, scale=0.25
    asterix::flight_level fl;
    fl.set_raw(1400);                              // FL350
    sub.set_flight_level(fl);

    // Sub-item: track-number (bit 2)
    sub.set_track_number(8888);

    // Sub-item: velocity (bit 1) — signed 16-bit vx/vy
    asterix::Cat253I100FormatE_sub_items_velocity vel;
    vel.set_vx(-200);
    vel.set_vy(300);
    sub.set_velocity(vel);

    items.set_i100(asterix::Cat253Record_items_i100Variant{fmt_e});

    // I110: Text annotation
    asterix::Cat253I110 text;
    text.set_text("EXTENDED FORMAT E");
    items.set_i110(text);

    // Third FSPEC octet: RE expansion field
    asterix::Cat253Record_items_re re_field;
    re_field.set_len(6);                           // 1 + 5 bytes
    re_field.set_data({0x11, 0x22, 0x33, 0x44, 0x55});
    items.set_re(re_field);

    // SP: Special Purpose field
    asterix::Cat253Record_items_sp sp_field;
    sp_field.set_len(4);                           // 1 + 3 bytes
    sp_field.set_data({0xAA, 0xBB, 0xCC});
    items.set_sp(sp_field);

    return rec;
}

// ============================================================================
// Verification functions
// ============================================================================

static bool verify_cat001(const asterix::Cat001Record& rec) {
    bool ok = true;

    auto& items = rec.items();

    ok &= items.has_i010();
    ok &= (items.i010().sac() == 0x12);
    ok &= (items.i010().sic() == 0x34);

    ok &= items.has_i020();
    ok &= (items.i020().typ() == asterix::cat001_report_type::track);

    ok &= items.has_i040();
    ok &= (std::abs(items.i040().rho() - 25000.0) < 2.0);

    ok &= items.has_i042();
    ok &= (items.i042().x() == 17678);
    ok &= (items.i042().y() == -17678);

    ok &= items.has_i070();
    ok &= (items.i070().v() == 1);
    ok &= (items.i070().g() == 0);
    ok &= (items.i070().l() == 1);
    ok &= (items.i070().code() == 01234);

    ok &= items.has_i090();
    ok &= (items.i090().v() == 1);

    ok &= items.has_i161();
    ok &= (items.i161() == 4567);

    // Second FSPEC octet
    ok &= items.has_i170();
    ok &= (items.i170().cnf() == 1);
    ok &= (items.i170().rad() == 2);
    ok &= (items.i170().man() == 1);
    ok &= (items.i170().tre() == 1);

    ok &= items.has_i141();
    ok &= (items.i141().raw() == 5529600);

    log("VERIFY", "Cat001Record: dsid=%02X/%02X polar.rho=%u track=%u fl.v=%u i170.cnf=%u [%s]",
        items.i010().sac(), items.i010().sic(),
        items.i040().rho(), items.i161(),
        items.i090().v(), items.i170().cnf(),
        ok ? "OK" : "FAIL");
    return ok;
}

static bool verify_cat048(const asterix::Cat048Record& rec) {
    bool ok = true;

    auto& items = rec.items();

    ok &= items.has_i010();
    ok &= (items.i010().sac() == 0x30);
    ok &= (items.i010().sic() == 0x40);

    ok &= items.has_i140();
    ok &= (items.i140().raw() == 3916800);

    ok &= items.has_i020();
    ok &= (items.i020().typ() == 3);
    ok &= (items.i020().spi() == 1);
    ok &= (items.i020().xpp() == 1);
    ok &= (items.i020().foe_fri() == 1);

    ok &= items.has_i040();
    ok &= (std::abs(items.i040().rho() - 60000.0) < 2.0);

    ok &= items.has_i070();
    ok &= (items.i070().code() == 07700);

    ok &= items.has_i090();

    ok &= items.has_i130();
    ok &= items.i130().sub().has_srl();
    ok &= (items.i130().sub().srl() == 200);
    ok &= items.i130().sub().has_srr();
    ok &= (items.i130().sub().srr() == 15);
    ok &= items.i130().sub().has_sam();
    ok &= (items.i130().sub().sam() == -30);
    ok &= items.i130().sub().has_prl();
    ok &= (items.i130().sub().prl() == 180);
    ok &= items.i130().sub().has_pam();
    ok &= (items.i130().sub().pam() == 42);
    ok &= items.i130().sub().has_rpd();
    ok &= (items.i130().sub().rpd() == -5);
    ok &= items.i130().sub().has_apd();
    ok &= (items.i130().sub().apd() == 3);

    // Second FSPEC octet
    ok &= items.has_i220();
    ok &= (items.i220() == 0xABCDEF);

    ok &= items.has_i240();

    ok &= items.has_i250();
    ok &= (items.i250().rep() == 2);
    ok &= (items.i250().bds().size() == 2);
    ok &= (items.i250().bds()[0].bds1() == 4);
    ok &= (items.i250().bds()[1].bds1() == 5);

    ok &= items.has_i161();
    ok &= (items.i161() == 9999);

    ok &= items.has_i042();
    ok &= (items.i042().x() == -5000);
    ok &= (items.i042().y() == 12000);

    ok &= items.has_i200();

    ok &= items.has_i170();
    ok &= (items.i170().rad() == 1);
    ok &= (items.i170().cdm() == 2);
    ok &= (items.i170().tre() == 1);
    ok &= (items.i170().sup() == 1);

    log("VERIFY", "Cat048Record: dsid=%02X/%02X addr=0x%06X track=%u bds_count=%zu [%s]",
        items.i010().sac(), items.i010().sic(),
        items.i220(), items.i161(),
        items.i250().bds().size(),
        ok ? "OK" : "FAIL");
    return ok;
}

static bool verify_cat253(const asterix::Cat253Record& rec) {
    auto& items = rec.items();

    if (!items.has_i100()) {
        log("VERIFY", "Cat253Record: no I100 choice present");
        return false;
    }

    // Determine which format was received
    if (std::holds_alternative<asterix::Cat253I100FormatA>(items.i100())) {
        auto& fmt = std::get<asterix::Cat253I100FormatA>(items.i100());
        bool ok = true;
        ok &= (fmt.data_type() == 7);
        for (size_t i = 0; i < 16; i++) {
            ok &= (fmt.payload()[i] == static_cast<uint8_t>(i * 17));
        }
        ok &= items.has_i060();
        ok &= (items.i060().operational() == 1);
        ok &= items.has_i070();
        ok &= (items.i070().sequence() == 0x0001);
        log("VERIFY", "Cat253 FormatA: data_type=%u [%s]", fmt.data_type(), ok ? "OK" : "FAIL");
        return ok;
    }
    else if (std::holds_alternative<asterix::Cat253I100FormatB>(items.i100())) {
        auto& fmt = std::get<asterix::Cat253I100FormatB>(items.i100());
        bool ok = true;
        ok &= (fmt.version() == 2);
        ok &= (fmt.flags() == 0xAB);
        ok &= (fmt.payload_length() == 8);
        ok &= (fmt.payload().size() == 8);
        ok &= (fmt.payload()[0] == 0xDE);
        ok &= (fmt.payload()[3] == 0xEF);
        ok &= (fmt.payload()[7] == 0xBE);
        log("VERIFY", "Cat253 FormatB: ver=%u flags=0x%02X len=%u [%s]",
            fmt.version(), fmt.flags(), fmt.payload_length(), ok ? "OK" : "FAIL");
        return ok;
    }
    else if (std::holds_alternative<asterix::Cat253I100FormatC>(items.i100())) {
        auto& fmt = std::get<asterix::Cat253I100FormatC>(items.i100());
        bool ok = true;
        ok &= (fmt.record_type() == 3);
        ok &= (fmt.record_count() == 3);
        ok &= (fmt.records().size() == 3);
        ok &= (fmt.records()[0].id() == 100);
        ok &= (fmt.records()[0].value() == 0xDEADBEEF);
        ok &= (fmt.records()[1].id() == 200);
        ok &= (fmt.records()[1].value() == 0xCAFEBABE);
        ok &= (fmt.records()[2].id() == 300);
        ok &= (fmt.records()[2].value() == 0x12345678);
        ok &= items.has_i090();
        ok &= (items.i090().priority() == 15);
        ok &= (items.i090().compressed() == 1);
        ok &= items.has_i110();
        ok &= (strip_nulls(items.i110().text()) == "FORMAT-C TEST");
        log("VERIFY", "Cat253 FormatC: type=%u count=%u recs=%zu [%s]",
            fmt.record_type(), fmt.record_count(), fmt.records().size(),
            ok ? "OK" : "FAIL");
        return ok;
    }
    else if (std::holds_alternative<asterix::Cat253I100FormatD>(items.i100())) {
        auto& fmt = std::get<asterix::Cat253I100FormatD>(items.i100());
        bool ok = true;
        ok &= (fmt.latitude().raw() == 614283000);
        ok &= (fmt.longitude().raw() == -5505000);
        // altitude: 35000 ft, scale=0.25 → raw=140000 → back to 35000.0
        ok &= (std::abs(fmt.altitude() - 35000.0) < 1.0);
        ok &= (std::abs(fmt.ground_speed() - 480.0) < 1.0);
        ok &= (std::abs(fmt.heading() - 90.0) < 0.1);
        ok &= items.has_i120();
        ok &= (items.i120().ref_type() == 5);
        ok &= (items.i120().ref_id() == 0xABCD1234);
        log("VERIFY", "Cat253 FormatD: lat=%d lon=%d alt=%.0f gs=%.0f hdg=%.1f [%s]",
            fmt.latitude().raw(), fmt.longitude().raw(),
            fmt.altitude(), fmt.ground_speed(), fmt.heading(),
            ok ? "OK" : "FAIL");
        return ok;
    }
    else if (std::holds_alternative<asterix::Cat253I100FormatE>(items.i100())) {
        auto& fmt = std::get<asterix::Cat253I100FormatE>(items.i100());
        bool ok = true;
        ok &= (fmt.format_version() == 4);

        auto& sub = fmt.sub_items();
        ok &= sub.has_source_id();
        ok &= (sub.source_id().sac() == 0xBB);
        ok &= (sub.source_id().sic() == 0xCC);

        ok &= sub.has_callsign();

        ok &= sub.has_aircraft_addr();
        ok &= (sub.aircraft_addr() == 0x4CA123);

        ok &= sub.has_position();
        ok &= (sub.position().lat().raw() == 500000000);
        ok &= (sub.position().lon().raw() == -100000000);

        ok &= sub.has_flight_level();
        ok &= (sub.flight_level().raw() == 1400);

        ok &= sub.has_track_number();
        ok &= (sub.track_number() == 8888);

        ok &= sub.has_velocity();
        ok &= (sub.velocity().vx() == -200);
        ok &= (sub.velocity().vy() == 300);

        // Verify I060
        ok &= items.has_i060();
        ok &= (items.i060().degraded() == 1);
        ok &= (items.i060().error_code() == 7);

        // Verify I070
        ok &= items.has_i070();
        ok &= (items.i070().sequence() == 0xFFFF);
        ok &= (items.i070().fragment() == 5);

        // Verify I090
        ok &= items.has_i090();
        ok &= (items.i090().encrypted() == 1);

        // Verify I110 text
        ok &= items.has_i110();
        ok &= (strip_nulls(items.i110().text()) == "EXTENDED FORMAT E");

        // Verify RE/SP expansion fields
        ok &= items.has_re();
        ok &= (items.re().len() == 6);
        ok &= (items.re().data().size() == 5);
        ok &= (items.re().data()[0] == 0x11);

        ok &= items.has_sp();
        ok &= (items.sp().len() == 4);
        ok &= (items.sp().data().size() == 3);
        ok &= (items.sp().data()[0] == 0xAA);

        log("VERIFY", "Cat253 FormatE: ver=%u src=%02X/%02X addr=0x%06X fl=%u trk=%u [%s]",
            fmt.format_version(),
            sub.source_id().sac(), sub.source_id().sic(),
            sub.aircraft_addr(),
            sub.flight_level().raw(),
            sub.track_number(),
            ok ? "OK" : "FAIL");
        return ok;
    }

    log("VERIFY", "Cat253Record: unknown I100 variant index");
    return false;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    log("MAIN", "ASTERIX Example — Encode/Decode Roundtrip");
    log("MAIN", "Protocol: ASTERIX v1.0 (3 categories: CAT001, CAT048, CAT253)");
    log("MAIN", "Categories: %u, %u, %u", asterix::CAT001, asterix::CAT048, asterix::CAT253);
    log("MAIN", "");

    bool all_pass = true;

    // ========================================================================
    // 1. Cat001Record roundtrip (all FSPEC items + FX chain)
    // ========================================================================

    {
        log("TEST", "Cat001Record: all items + FX extension");
        auto rec = make_cat001_full();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat001(*decoded);
        }
    }

    // ========================================================================
    // 2. Cat048Record roundtrip (FX chains, nested bitmap, arrays, packed strings)
    // ========================================================================

    {
        log("TEST", "Cat048Record: FX chains, nested bitmap, BDS array, packed ident");
        auto rec = make_cat048_full();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat048(*decoded);
        }
    }

    // ========================================================================
    // 3. Cat253Records — all 5 I100 format variants
    // ========================================================================

    {
        log("TEST", "Cat253Record FormatA: selector=42, fixed payload");
        auto rec = make_cat253_format_a();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat253(*decoded);
        }
    }

    {
        log("TEST", "Cat253Record FormatB: selector=300, variable payload");
        auto rec = make_cat253_format_b();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat253(*decoded);
        }
    }

    {
        log("TEST", "Cat253Record FormatC: selector=750, 3 records array");
        auto rec = make_cat253_format_c();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat253(*decoded);
        }
    }

    {
        log("TEST", "Cat253Record FormatD: selector=1500, position data");
        auto rec = make_cat253_format_d();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat253(*decoded);
        }
    }

    {
        log("TEST", "Cat253Record FormatE: selector=3000, nested bitmap + RE/SP");
        auto rec = make_cat253_format_e();
        auto decoded = roundtrip(rec);
        if (!decoded) {
            log("TEST", "  roundtrip failed: %s", decoded.error().format_short().c_str());
            all_pass = false;
        } else {
            all_pass &= verify_cat253(*decoded);
        }
    }

    // ========================================================================
    // 4. AsterixFrame roundtrip (multi-category DataBlocks)
    // ========================================================================

    {
        log("TEST", "AsterixFrame: multi-category DataBlock roundtrip");

        // Wrap Cat001 + Cat048 into DataBlocks inside an AsterixFrame
        auto cat001_bytes = make_cat001_full().encode_bytes();
        auto cat048_bytes = make_cat048_full().encode_bytes();
        if (!cat001_bytes || !cat048_bytes) {
            log("TEST", "  encode failed");
            all_pass = false;
        } else {
            asterix::DataBlock db1;
            db1.set_cat(asterix::CAT001);
            db1.set_len(static_cast<asterix::uint16>(3 + cat001_bytes->size()));
            asterix::DataBlock_cat001 cat001_payload;
            cat001_payload.mutable_items().push_back(make_cat001_full());
            db1.set_records(std::move(cat001_payload));

            asterix::DataBlock db2;
            db2.set_cat(asterix::CAT048);
            db2.set_len(static_cast<asterix::uint16>(3 + cat048_bytes->size()));
            asterix::DataBlock_cat048 cat048_payload;
            cat048_payload.mutable_items().push_back(make_cat048_full());
            db2.set_records(std::move(cat048_payload));

            asterix::AsterixFrame frame;
            frame.mutable_blocks().push_back(std::move(db1));
            frame.mutable_blocks().push_back(std::move(db2));

            auto frame_decoded = roundtrip(frame);
            if (!frame_decoded) {
                log("TEST", "  frame roundtrip failed: %s",
                    frame_decoded.error().format_short().c_str());
                all_pass = false;
            } else {
                bool ok = (frame_decoded->blocks().size() == 2);
                ok &= (frame_decoded->blocks()[0].cat() == asterix::CAT001);
                ok &= (frame_decoded->blocks()[1].cat() == asterix::CAT048);
                log("VERIFY", "AsterixFrame: %zu blocks, cat[0]=%u cat[1]=%u [%s]",
                    frame_decoded->blocks().size(),
                    frame_decoded->blocks()[0].cat(),
                    frame_decoded->blocks()[1].cat(),
                    ok ? "OK" : "FAIL");
                all_pass &= ok;
            }
        }
    }

    // ========================================================================
    // 5. Summary
    // ========================================================================

    log("MAIN", "");
    if (all_pass) {
        log("MAIN", "ALL CHECKS PASSED");
    } else {
        log("MAIN", "SOME CHECKS FAILED");
    }

    return all_pass ? 0 : 1;
}
