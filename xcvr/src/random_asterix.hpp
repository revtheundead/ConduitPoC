#pragma once

// Random ASTERIX message generators for PoC testing.
// Each function creates a message with mandatory items always set and optional
// items included with ~50% probability.  Compound sub-structs are auto-emplaced
// with defaults; key fields are randomized to exercise encoding diversity.

#include "asterix.hpp"
#include <random>
#include <string>

namespace random_asterix {

// ── Helpers ─────────────────────────────────────────────────────────────────

inline uint8_t rand_u8(std::mt19937& rng, uint8_t max = 255) {
    return static_cast<uint8_t>(std::uniform_int_distribution<unsigned>(0, max)(rng));
}

inline uint16_t rand_u16(std::mt19937& rng, uint16_t max = 0xFFFF) {
    return static_cast<uint16_t>(std::uniform_int_distribution<unsigned>(0, max)(rng));
}

inline uint32_t rand_u32(std::mt19937& rng, uint32_t max = 0xFFFFFFFF) {
    return std::uniform_int_distribution<uint32_t>(0, max)(rng);
}

inline int8_t rand_i8(std::mt19937& rng, int8_t lo = -128, int8_t hi = 127) {
    return static_cast<int8_t>(std::uniform_int_distribution<int>(lo, hi)(rng));
}

inline int16_t rand_i16(std::mt19937& rng, int16_t lo = -32768, int16_t hi = 32767) {
    return static_cast<int16_t>(std::uniform_int_distribution<int>(lo, hi)(rng));
}

inline bool rand_bool(std::mt19937& rng) {
    return std::uniform_int_distribution<int>(0, 1)(rng) == 1;
}

// Random 24-bit value (aircraft address range)
inline uint32_t rand_u24(std::mt19937& rng) {
    return rand_u32(rng, 0xFFFFFF);
}

// Random time-of-day raw value (24-bit, ~0 to 86400 seconds range)
inline asterix::TimeOfDay rand_time_of_day(std::mt19937& rng) {
    asterix::TimeOfDay tod;
    // Max raw value = 86400 / 0.0078125 = 11059200 (0xA8C000)
    tod.set_raw(rand_u32(rng, 11059200));
    return tod;
}

// Random DataSourceId
inline asterix::DataSourceId rand_data_source_id(std::mt19937& rng) {
    asterix::DataSourceId ds;
    ds.set_sac(rand_u8(rng));
    ds.set_sic(rand_u8(rng));
    return ds;
}

// 6-bit ICAO character set: space (0x20) and uppercase A-Z (0x41-0x5A)
inline std::string rand_icao_str(std::mt19937& rng) {
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(chars) - 2));
    std::string s;
    s.reserve(8);
    for (int i = 0; i < 8; ++i) s += chars[dist(rng)];
    // Trim trailing spaces
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

inline asterix::AircraftIdent rand_aircraft_ident(std::mt19937& rng) {
    asterix::AircraftIdent ai;
    ai.set_value(rand_icao_str(rng));
    return ai;
}

// ── RE field helpers ────────────────────────────────────────────────────────

// Random 32-bit signed value clamped to a bit width
inline int32_t rand_signed(std::mt19937& rng, int bits) {
    int32_t lo = -(1 << (bits - 1));
    int32_t hi = (1 << (bits - 1)) - 1;
    return std::uniform_int_distribution<int32_t>(lo, hi)(rng);
}

inline void rand_fill_cat048_re(std::mt19937& rng,
                                asterix::Cat048Record_items_re_items& re) {
    // MD5 or M5N (mutually exclusive, ~25% each)
    int mode5_pick = std::uniform_int_distribution<int>(0, 3)(rng);
    if (mode5_pick == 0) {
        auto& md5 = re.mutable_md5().mutable_sub();
        if (rand_bool(rng)) {
            auto& sum = md5.mutable_sum();
            sum.set_m5(rand_u8(rng, 1));
            sum.set_id(rand_u8(rng, 1));
            sum.set_da(rand_u8(rng, 1));
            sum.set_m1(rand_u8(rng, 1));
            sum.set_m2(rand_u8(rng, 1));
            sum.set_m3(rand_u8(rng, 1));
            sum.set_mc(rand_u8(rng, 1));
        }
        if (rand_bool(rng)) {
            auto& pmn = md5.mutable_pmn();
            pmn.set_pin(rand_u16(rng, 0x3FFF));
            pmn.set_nav(static_cast<asterix::NatOriginValidity>(rand_u8(rng, 1)));
            pmn.set_nat(rand_u8(rng, 31));
            pmn.set_mis(rand_u8(rng, 63));
        }
        if (rand_bool(rng)) {
            auto& pos = md5.mutable_pos();
            pos.set_lat(std::uniform_real_distribution<double>(-90.0, 90.0)(rng));
            pos.set_lon(std::uniform_real_distribution<double>(-180.0, 180.0)(rng));
        }
        if (rand_bool(rng)) {
            auto& ga = md5.mutable_ga();
            ga.set_res(static_cast<asterix::AltResolution>(rand_u8(rng, 1)));
            ga.set_ga(rand_signed(rng, 14) * 25.0);
        }
        if (rand_bool(rng)) {
            auto& em1 = md5.mutable_em1();
            em1.set_v(static_cast<asterix::CodeValidated>(rand_u8(rng, 1)));
            em1.set_g(static_cast<asterix::CodeGarbled>(rand_u8(rng, 1)));
            em1.set_l(static_cast<asterix::CodeSourceExtracted>(rand_u8(rng, 1)));
            em1.set_em1(rand_u16(rng, 0xFFF));
        }
        if (rand_bool(rng)) {
            md5.set_tos(std::uniform_real_distribution<double>(-1.0, 1.0)(rng));
        }
        if (rand_bool(rng)) {
            auto& xp = md5.mutable_xp();
            xp.set_xp(rand_u8(rng, 1));
            xp.set_x5(rand_u8(rng, 1));
            xp.set_xc(rand_u8(rng, 1));
            xp.set_x3(rand_u8(rng, 1));
            xp.set_x2(rand_u8(rng, 1));
            xp.set_x1(rand_u8(rng, 1));
        }
    } else if (mode5_pick == 1) {
        auto& m5n = re.mutable_m5n().mutable_sub();
        if (rand_bool(rng)) {
            auto& sum = m5n.mutable_sum();
            sum.set_m5(rand_u8(rng, 1));
            sum.set_id(rand_u8(rng, 1));
            sum.set_da(rand_u8(rng, 1));
            sum.set_m1(rand_u8(rng, 1));
            sum.set_m2(rand_u8(rng, 1));
            sum.set_m3(rand_u8(rng, 1));
            sum.set_mc(rand_u8(rng, 1));
        }
        if (rand_bool(rng)) {
            auto& pmn = m5n.mutable_pmn();
            pmn.set_pin(rand_u16(rng, 0x3FFF));
            pmn.set_nov(static_cast<asterix::NatOriginValidity>(rand_u8(rng, 1)));
            pmn.set_no(rand_u16(rng, 0x7FF));
        }
        if (rand_bool(rng)) {
            auto& pos = m5n.mutable_pos();
            pos.set_lat(std::uniform_real_distribution<double>(-90.0, 90.0)(rng));
            pos.set_lon(std::uniform_real_distribution<double>(-180.0, 180.0)(rng));
        }
        if (rand_bool(rng)) {
            auto& ga = m5n.mutable_ga();
            ga.set_res(static_cast<asterix::AltResolution>(rand_u8(rng, 1)));
            ga.set_ga(rand_signed(rng, 14) * 25.0);
        }
        if (rand_bool(rng)) {
            auto& em1 = m5n.mutable_em1();
            em1.set_v(static_cast<asterix::CodeValidated>(rand_u8(rng, 1)));
            em1.set_g(static_cast<asterix::CodeGarbled>(rand_u8(rng, 1)));
            em1.set_l(static_cast<asterix::CodeSourceExtracted>(rand_u8(rng, 1)));
            em1.set_em1(rand_u16(rng, 0xFFF));
        }
        if (rand_bool(rng)) {
            m5n.set_tos(std::uniform_real_distribution<double>(-1.0, 1.0)(rng));
        }
        if (rand_bool(rng)) {
            auto& xp = m5n.mutable_xp();
            xp.set_xp(rand_u8(rng, 1));
            xp.set_x5(rand_u8(rng, 1));
            xp.set_xc(rand_u8(rng, 1));
            xp.set_x3(rand_u8(rng, 1));
            xp.set_x2(rand_u8(rng, 1));
            xp.set_x1(rand_u8(rng, 1));
        }
        if (rand_bool(rng)) {
            m5n.mutable_fom().set_fom(rand_u8(rng, 31));
        }
    }

    // M4E (~50%)
    if (rand_bool(rng)) {
        re.mutable_m4e().set_foeFri(static_cast<asterix::FoeFriId>(rand_u8(rng, 3)));
    }

    // RPC (~50%)
    if (rand_bool(rng)) {
        auto& rpc = re.mutable_rpc().mutable_sub();
        if (rand_bool(rng)) rpc.set_sco(rand_u8(rng));
        if (rand_bool(rng)) rpc.set_scr(rand_u16(rng, 6553) * 0.1);
        if (rand_bool(rng)) rpc.set_rw(rand_u16(rng) * 0.00390625);
        if (rand_bool(rng)) rpc.set_ar(rand_u16(rng) * 0.00390625);
    }

    // ERR (~50%)
    if (rand_bool(rng)) {
        re.mutable_err().set_rho(rand_u24(rng) * 0.00390625);
    }
}

inline void rand_fill_cat021_re(std::mt19937& rng,
                                asterix::Cat021Record_items_re_items& re) {
    // BPS (~50%)
    if (rand_bool(rng)) {
        // BPS value 0-4095 => 0.0 to 409.5 hPa (offset from 800)
        re.mutable_bps().set_bps(rand_u16(rng, 4095) * 0.1);
    }

    // SelH (~50%)
    if (rand_bool(rng)) {
        auto& selh = re.mutable_selh();
        selh.set_hrd(static_cast<asterix::Cat021NorthRef>(rand_u8(rng, 1)));
        selh.set_stat(rand_u8(rng, 1));
        selh.set_selh(std::uniform_real_distribution<double>(0.0, 359.296875)(rng));
    }

    // NAV (~50%)
    if (rand_bool(rng)) {
        auto& nav = re.mutable_nav();
        nav.set_ap(rand_u8(rng, 1));
        nav.set_vn(rand_u8(rng, 1));
        nav.set_ah(rand_u8(rng, 1));
        nav.set_am(rand_u8(rng, 1));
    }

    // GAO (~50%)
    if (rand_bool(rng)) {
        re.set_gao(rand_u8(rng));
    }

    // SGV (~50%)
    if (rand_bool(rng)) {
        auto& sgv = re.mutable_sgv();
        sgv.set_stp(rand_u8(rng, 1));
        sgv.set_hts(rand_u8(rng, 1));
        sgv.set_htt(static_cast<asterix::Cat021NorthRef>(rand_u8(rng, 1)));
        sgv.set_hrd(static_cast<asterix::Cat021NorthRef>(rand_u8(rng, 1)));
        sgv.set_gss(rand_u16(rng, 2047) * 0.125);
        // HGT extension (~50%)
        if (rand_bool(rng)) {
            sgv.set_hgt(rand_u8(rng, 127) * 2.8125);
        }
    }

    // STA (~50%)
    if (rand_bool(rng)) {
        auto& sta = re.mutable_sta();
        sta.set_es(rand_u8(rng, 1));
        sta.set_uat(rand_u8(rng, 1));
    }

    // TNH (~50%)
    if (rand_bool(rng)) {
        re.set_tnh(std::uniform_real_distribution<double>(0.0, 360.0)(rng));
    }

    // MES (~30%)
    if (rand_bool(rng) && rand_bool(rng)) {
        auto& mes = re.mutable_mes().mutable_sub();
        if (rand_bool(rng)) {
            auto& sum = mes.mutable_sum();
            sum.set_m5(rand_u8(rng, 1));
            sum.set_id(rand_u8(rng, 1));
            sum.set_da(rand_u8(rng, 1));
            sum.set_m1(rand_u8(rng, 1));
            sum.set_m2(rand_u8(rng, 1));
            sum.set_m3(rand_u8(rng, 1));
            sum.set_mc(rand_u8(rng, 1));
            sum.set_po(rand_u8(rng, 1));
        }
        if (rand_bool(rng)) {
            auto& pno = mes.mutable_pno();
            pno.set_pin(rand_u16(rng, 0x3FFF));
            pno.set_no(rand_u16(rng, 0x7FF));
        }
        if (rand_bool(rng)) {
            auto& em1 = mes.mutable_em1();
            em1.set_v(static_cast<asterix::CodeValidated>(rand_u8(rng, 1)));
            em1.set_l(static_cast<asterix::CodeSourceExtracted>(rand_u8(rng, 1)));
            em1.set_em1(rand_u16(rng, 0xFFF));
        }
        if (rand_bool(rng)) {
            auto& xp = mes.mutable_xp();
            xp.set_xp(rand_u8(rng, 1));
            xp.set_x5(rand_u8(rng, 1));
            xp.set_xc(rand_u8(rng, 1));
            xp.set_x3(rand_u8(rng, 1));
            xp.set_x2(rand_u8(rng, 1));
            xp.set_x1(rand_u8(rng, 1));
        }
        if (rand_bool(rng)) {
            mes.mutable_fom().set_fom(rand_u8(rng, 31));
        }
        if (rand_bool(rng)) {
            auto& m2 = mes.mutable_m2();
            m2.set_v(static_cast<asterix::CodeValidated>(rand_u8(rng, 1)));
            m2.set_l(static_cast<asterix::CodeSourceExtracted>(rand_u8(rng, 1)));
            m2.set_mode2(rand_u16(rng, 0xFFF));
        }
    }
}

// ── Array element helpers ────────────────────────────────────────────────────

template <typename T>
inline void rand_fill_bds(std::mt19937& rng, T& i250) {
    using ElemType = typename std::decay_t<decltype(i250.mutable_bds())>::value_type;
    int n = std::uniform_int_distribution<int>(1, 3)(rng);
    i250.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        ElemType elem;
        uint64_t data = 0;
        for (int k = 0; k < 7; ++k)
            data = (data << 8) | rand_u8(rng);
        elem.set_mbData(data);
        elem.set_bds1(rand_u8(rng, 15));
        elem.set_bds2(rand_u8(rng, 15));
        i250.mutable_bds().push_back(elem);
    }
}

inline void rand_fill_registers(std::mt19937& rng, asterix::Cat007I440& i440) {
    int n = std::uniform_int_distribution<int>(1, 4)(rng);
    i440.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        asterix::Cat007I440_registersElement elem;
        elem.set_bds1(rand_u8(rng, 15));
        elem.set_bds2(rand_u8(rng, 15));
        i440.mutable_registers().push_back(elem);
    }
}

inline void rand_fill_destinations(std::mt19937& rng, asterix::Cat253I025& i025) {
    int n = std::uniform_int_distribution<int>(1, 3)(rng);
    i025.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        asterix::Cat253I025_destinationsElement elem;
        elem.set_sac(rand_u8(rng));
        elem.set_sic(rand_u8(rng));
        elem.set_localId(rand_u8(rng));
        i025.mutable_destinations().push_back(elem);
    }
}

inline void rand_fill_sequences(std::mt19937& rng, asterix::Cat253I050& i050) {
    int n = std::uniform_int_distribution<int>(1, 4)(rng);
    i050.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        asterix::Cat253I050_sequencesElement elem;
        elem.set_msid(rand_u16(rng));
        i050.mutable_sequences().push_back(elem);
    }
}

// ── Cat007 Downlink Record ──────────────────────────────────────────────────

inline asterix::Cat007DownlinkRecord random_cat007_downlink(std::mt19937& rng) {
    asterix::Cat007DownlinkRecord rec;
    auto& it = rec.mutable_items();

    // Mandatory: I010 (Data Source Id)
    it.set_i010(rand_data_source_id(rng));

    // Mandatory: I140 (Time of Day)
    it.set_i140(rand_time_of_day(rng));

    // Optional items (~50% each)
    if (rand_bool(rng)) it.set_i025(rand_data_source_id(rng));
    if (rand_bool(rng)) (void)it.mutable_i410();  // auto-emplace defaults
    if (rand_bool(rng)) (void)it.mutable_i400();
    if (rand_bool(rng)) {
        auto& i020 = it.mutable_i020();
        i020.set_typ(static_cast<asterix::DetectionType>(rand_u8(rng, 7)));
        i020.set_sim(static_cast<asterix::SimIndicator>(rand_u8(rng, 1)));
        i020.set_rdp(static_cast<asterix::RdpChain>(rand_u8(rng, 1)));
        i020.set_spi(static_cast<asterix::SpiPresence>(rand_u8(rng, 1)));
        i020.set_rab(static_cast<asterix::ReportSource>(rand_u8(rng, 1)));
        // FX extension fields (~50%)
        if (rand_bool(rng)) {
            i020.set_tst(static_cast<asterix::TestTarget>(rand_u8(rng, 1)));
            i020.set_err(static_cast<asterix::ExtendedRange>(rand_u8(rng, 1)));
            i020.set_xpp(static_cast<asterix::XPulsePresence>(rand_u8(rng, 1)));
            i020.set_me(static_cast<asterix::MilitaryEmergency>(rand_u8(rng, 1)));
            i020.set_mi(static_cast<asterix::MilitaryId>(rand_u8(rng, 1)));
            i020.set_foeFri(static_cast<asterix::FoeFriId>(rand_u8(rng, 3)));
        }
    }
    if (rand_bool(rng)) {
        auto& i040 = it.mutable_i040();
        i040.set_rho(std::uniform_real_distribution<double>(0.0, 256.0)(rng));
        i040.set_theta(std::uniform_real_distribution<double>(0.0, 360.0)(rng));
    }
    if (rand_bool(rng)) (void)it.mutable_i070();
    if (rand_bool(rng)) (void)it.mutable_i090();
    if (rand_bool(rng)) (void)it.mutable_i130();
    if (rand_bool(rng)) it.set_i220(rand_u24(rng));        // aircraft address
    if (rand_bool(rng)) it.set_i240(rand_aircraft_ident(rng));
    if (rand_bool(rng)) rand_fill_bds(rng, it.mutable_i250());
    if (rand_bool(rng)) (void)it.mutable_i161();
    if (rand_bool(rng)) {
        auto& i042 = it.mutable_i042();
        i042.set_x(std::uniform_real_distribution<double>(-200.0, 200.0)(rng));
        i042.set_y(std::uniform_real_distribution<double>(-200.0, 200.0)(rng));
    }
    if (rand_bool(rng)) (void)it.mutable_i200();
    if (rand_bool(rng)) (void)it.mutable_i170();
    if (rand_bool(rng)) (void)it.mutable_i210();
    if (rand_bool(rng)) (void)it.mutable_i030();
    if (rand_bool(rng)) (void)it.mutable_i080();
    if (rand_bool(rng)) (void)it.mutable_i100();
    if (rand_bool(rng)) (void)it.mutable_i110();
    if (rand_bool(rng)) (void)it.mutable_i120();
    if (rand_bool(rng)) (void)it.mutable_i230();
    if (rand_bool(rng)) (void)it.mutable_i260();
    if (rand_bool(rng)) (void)it.mutable_i055();
    if (rand_bool(rng)) (void)it.mutable_i050();
    if (rand_bool(rng)) (void)it.mutable_i065();
    if (rand_bool(rng)) (void)it.mutable_i060();
    if (rand_bool(rng)) (void)it.mutable_i450();
    if (rand_bool(rng)) (void)it.mutable_i085();

    return rec;
}

// ── Cat007 Uplink Record ────────────────────────────────────────────────────

inline asterix::Cat007UplinkRecord random_cat007_uplink(std::mt19937& rng) {
    asterix::Cat007UplinkRecord rec;
    auto& it = rec.mutable_items();

    // Mandatory
    it.set_i010(rand_data_source_id(rng));
    it.set_i140(rand_time_of_day(rng));

    // Optional
    if (rand_bool(rng)) it.set_i025(rand_data_source_id(rng));
    if (rand_bool(rng)) (void)it.mutable_i410();
    if (rand_bool(rng)) (void)it.mutable_i400();
    if (rand_bool(rng)) {
        auto& i040 = it.mutable_i040();
        i040.set_rho(std::uniform_real_distribution<double>(0.0, 256.0)(rng));
        i040.set_theta(std::uniform_real_distribution<double>(0.0, 360.0)(rng));
    }
    if (rand_bool(rng)) it.set_i220(rand_u24(rng));
    if (rand_bool(rng)) (void)it.mutable_i161();
    if (rand_bool(rng)) {
        auto& i042 = it.mutable_i042();
        i042.set_x(std::uniform_real_distribution<double>(-200.0, 200.0)(rng));
        i042.set_y(std::uniform_real_distribution<double>(-200.0, 200.0)(rng));
    }
    if (rand_bool(rng)) (void)it.mutable_i200();
    if (rand_bool(rng)) (void)it.mutable_i415();
    if (rand_bool(rng)) (void)it.mutable_i420();
    if (rand_bool(rng)) rand_fill_registers(rng, it.mutable_i440());

    return rec;
}

// ── Cat021 Record ───────────────────────────────────────────────────────────

inline asterix::Cat021Record random_cat021(std::mt19937& rng) {
    asterix::Cat021Record rec;
    auto& it = rec.mutable_items();

    // Mandatory
    it.set_i010(rand_data_source_id(rng));
    it.set_i071(rand_time_of_day(rng));  // Time of Applicability for Position

    // Optional items
    if (rand_bool(rng)) (void)it.mutable_i040();
    if (rand_bool(rng)) (void)it.mutable_i161();
    if (rand_bool(rng)) it.set_i015(rand_u8(rng));
    if (rand_bool(rng)) {
        auto& i130 = it.mutable_i130();
        i130.set_latitude(std::uniform_real_distribution<double>(-90.0, 90.0)(rng));
        i130.set_longitude(std::uniform_real_distribution<double>(-180.0, 180.0)(rng));
    }
    if (rand_bool(rng)) (void)it.mutable_i131();
    if (rand_bool(rng)) it.set_i072(rand_time_of_day(rng));
    if (rand_bool(rng)) (void)it.mutable_i150();
    if (rand_bool(rng)) (void)it.mutable_i151();
    if (rand_bool(rng)) it.set_i080(rand_u24(rng));  // aircraft address
    if (rand_bool(rng)) it.set_i073(rand_time_of_day(rng));
    if (rand_bool(rng)) (void)it.mutable_i074();
    if (rand_bool(rng)) it.set_i075(rand_time_of_day(rng));
    if (rand_bool(rng)) (void)it.mutable_i076();
    if (rand_bool(rng)) (void)it.mutable_i140();
    if (rand_bool(rng)) (void)it.mutable_i090();
    if (rand_bool(rng)) (void)it.mutable_i210();
    if (rand_bool(rng)) (void)it.mutable_i070();
    if (rand_bool(rng)) (void)it.mutable_i230();
    if (rand_bool(rng)) (void)it.mutable_i145();
    if (rand_bool(rng)) (void)it.mutable_i152();
    if (rand_bool(rng)) (void)it.mutable_i200();
    if (rand_bool(rng)) (void)it.mutable_i155();
    if (rand_bool(rng)) (void)it.mutable_i157();
    if (rand_bool(rng)) (void)it.mutable_i160();
    if (rand_bool(rng)) (void)it.mutable_i165();
    if (rand_bool(rng)) it.set_i077(rand_time_of_day(rng));
    if (rand_bool(rng)) it.set_i170(rand_aircraft_ident(rng));
    if (rand_bool(rng)) it.set_i020(rand_u8(rng));
    if (rand_bool(rng)) (void)it.mutable_i220();
    if (rand_bool(rng)) (void)it.mutable_i146();
    if (rand_bool(rng)) (void)it.mutable_i148();
    if (rand_bool(rng)) (void)it.mutable_i110();
    if (rand_bool(rng)) (void)it.mutable_i016();
    if (rand_bool(rng)) (void)it.mutable_i008();
    if (rand_bool(rng)) (void)it.mutable_i271();
    if (rand_bool(rng)) it.set_i132(rand_i8(rng));
    if (rand_bool(rng)) rand_fill_bds(rng, it.mutable_i250());
    if (rand_bool(rng)) (void)it.mutable_i260();
    if (rand_bool(rng)) it.set_i400(rand_u8(rng));
    if (rand_bool(rng)) (void)it.mutable_i295();

    // RE - Reserved Expansion Field (~50%)
    if (rand_bool(rng)) {
        rand_fill_cat021_re(rng, it.mutable_re().mutable_items());
    }

    return rec;
}

// ── Cat048 Record ───────────────────────────────────────────────────────────

inline asterix::Cat048Record random_cat048(std::mt19937& rng) {
    asterix::Cat048Record rec;
    auto& it = rec.mutable_items();

    // Mandatory
    it.set_i010(rand_data_source_id(rng));
    it.set_i140(rand_time_of_day(rng));

    // Optional items
    if (rand_bool(rng)) (void)it.mutable_i020();
    if (rand_bool(rng)) {
        auto& i040 = it.mutable_i040();
        i040.set_rho(std::uniform_real_distribution<double>(0.0, 256.0)(rng));
        i040.set_theta(std::uniform_real_distribution<double>(0.0, 360.0)(rng));
    }
    if (rand_bool(rng)) (void)it.mutable_i070();
    if (rand_bool(rng)) (void)it.mutable_i090();
    if (rand_bool(rng)) (void)it.mutable_i130();
    if (rand_bool(rng)) it.set_i220(rand_u24(rng));
    if (rand_bool(rng)) it.set_i240(rand_aircraft_ident(rng));
    if (rand_bool(rng)) rand_fill_bds(rng, it.mutable_i250());
    if (rand_bool(rng)) (void)it.mutable_i161();
    if (rand_bool(rng)) {
        auto& i042 = it.mutable_i042();
        i042.set_x(std::uniform_real_distribution<double>(-200.0, 200.0)(rng));
        i042.set_y(std::uniform_real_distribution<double>(-200.0, 200.0)(rng));
    }
    if (rand_bool(rng)) (void)it.mutable_i200();
    if (rand_bool(rng)) (void)it.mutable_i170();
    if (rand_bool(rng)) (void)it.mutable_i210();
    if (rand_bool(rng)) (void)it.mutable_i030();
    if (rand_bool(rng)) (void)it.mutable_i080();
    if (rand_bool(rng)) (void)it.mutable_i100();
    if (rand_bool(rng)) (void)it.mutable_i110();
    if (rand_bool(rng)) (void)it.mutable_i120();
    if (rand_bool(rng)) (void)it.mutable_i230();
    if (rand_bool(rng)) (void)it.mutable_i260();
    if (rand_bool(rng)) (void)it.mutable_i055();
    if (rand_bool(rng)) (void)it.mutable_i050();
    if (rand_bool(rng)) (void)it.mutable_i065();
    if (rand_bool(rng)) (void)it.mutable_i060();

    // RE - Reserved Expansion Field (~50%)
    if (rand_bool(rng)) {
        rand_fill_cat048_re(rng, it.mutable_re().mutable_items());
    }

    return rec;
}

// ── Cat253 Record ───────────────────────────────────────────────────────────

inline asterix::Cat253Record random_cat253(std::mt19937& rng) {
    asterix::Cat253Record rec;
    auto& it = rec.mutable_items();

    // Mandatory
    it.set_i010(rand_data_source_id(rng));
    it.set_i070(rand_time_of_day(rng));

    // Optional items
    if (rand_bool(rng)) it.set_i015(rand_u8(rng));
    if (rand_bool(rng)) rand_fill_destinations(rng, it.mutable_i025());
    if (rand_bool(rng)) it.set_i030(rand_u16(rng));
    if (rand_bool(rng)) (void)it.mutable_i040();
    if (rand_bool(rng)) rand_fill_sequences(rng, it.mutable_i050());
    if (rand_bool(rng)) (void)it.mutable_i035();
    if (rand_bool(rng)) (void)it.mutable_i060();
    // I080 + I100 are linked: I100 choice dispatches on I080's start-index
    if (rand_bool(rng)) {
        int variant = std::uniform_int_distribution<int>(0, 2)(rng);
        constexpr uint16_t start_indices[] = {5, 6, 35};

        auto& i080 = it.mutable_i080();
        i080.set_startIndex(start_indices[variant]);
        i080.set_count(rand_u8(rng));
        i080.set_stale(static_cast<asterix::Cat253StaleInd>(rand_bool(rng) ? 1 : 0));
        i080.set_sim(static_cast<asterix::SimIndicator>(rand_bool(rng) ? 1 : 0));
        i080.set_localCtrl(static_cast<asterix::Cat253LocalCtrl>(rand_bool(rng) ? 1 : 0));
        i080.set_dataIncluded(asterix::Cat253DataIncl::DataIncluded);

        auto& i100 = it.mutable_i100();
        switch (variant) {
        case 0:
            i100.set_payload(asterix::Cat253Multipath{});
            break;
        case 1:
            i100.set_payload(asterix::Cat253Squitter{});
            break;
        case 2:
            i100.set_payload(asterix::Cat253BitReport{});
            break;
        }
    } else if (rand_bool(rng)) {
        (void)it.mutable_i080();
    }
    if (rand_bool(rng)) (void)it.mutable_i090();

    return rec;
}

} // namespace random_asterix
