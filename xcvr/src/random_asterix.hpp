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
inline asterix::time_of_day rand_time_of_day(std::mt19937& rng) {
    asterix::time_of_day tod;
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

inline asterix::aircraft_ident rand_aircraft_ident(std::mt19937& rng) {
    asterix::aircraft_ident ai;
    ai.set_value(rand_icao_str(rng));
    return ai;
}

// ── Array element helpers ────────────────────────────────────────────────────

template <typename T>
inline void rand_fill_bds(std::mt19937& rng, T& i250) {
    using ElemType = typename std::decay_t<decltype(i250.mutable_bds())>::value_type;
    int n = std::uniform_int_distribution<int>(1, 3)(rng);
    i250.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        ElemType elem;
        std::array<uint8_t, 7> data;
        for (auto& b : data) b = rand_u8(rng);
        elem.set_mb_data(data);
        elem.set_bds1(rand_u8(rng, 15));
        elem.set_bds2(rand_u8(rng, 15));
        i250.mutable_bds().push_back(elem);
    }
}

inline void rand_fill_registers(std::mt19937& rng, asterix::Cat007I440& i440) {
    int n = std::uniform_int_distribution<int>(1, 4)(rng);
    i440.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        asterix::registersElement elem;
        elem.set_bds1(rand_u8(rng, 15));
        elem.set_bds2(rand_u8(rng, 15));
        i440.mutable_registers().push_back(elem);
    }
}

inline void rand_fill_destinations(std::mt19937& rng, asterix::Cat253I025& i025) {
    int n = std::uniform_int_distribution<int>(1, 3)(rng);
    i025.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        asterix::destinationsElement elem;
        elem.set_sac(rand_u8(rng));
        elem.set_sic(rand_u8(rng));
        elem.set_local_id(rand_u8(rng));
        i025.mutable_destinations().push_back(elem);
    }
}

inline void rand_fill_sequences(std::mt19937& rng, asterix::Cat253I050& i050) {
    int n = std::uniform_int_distribution<int>(1, 4)(rng);
    i050.set_rep(static_cast<uint8_t>(n));
    for (int j = 0; j < n; ++j) {
        asterix::sequencesElement elem;
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
        i020.set_typ(rand_u8(rng, 7));   // 3 bits
        i020.set_sim(rand_u8(rng, 1));
        i020.set_rdp(rand_u8(rng, 1));
        i020.set_spi(rand_u8(rng, 1));
        i020.set_rab(rand_u8(rng, 1));
        // FX extension fields (~50%)
        if (rand_bool(rng)) {
            i020.set_tst(rand_u8(rng, 1));
            i020.set_err(rand_u8(rng, 1));
            i020.set_xpp(rand_u8(rng, 1));
            i020.set_me(rand_u8(rng, 1));
            i020.set_mi(rand_u8(rng, 1));
            i020.set_foe_fri(rand_u8(rng, 3));  // 2 bits
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
    if (rand_bool(rng)) (void)it.mutable_i080();
    if (rand_bool(rng)) (void)it.mutable_i090();

    return rec;
}

} // namespace random_asterix
