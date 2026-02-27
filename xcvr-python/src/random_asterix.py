"""Random ASTERIX message generators for PoC testing (client perspective).

Each function creates a message with mandatory items always set and optional
items included with ~50% probability.  Mirrors xcvr/src/random_asterix.hpp.
"""

import random
from conduit.generated.asterix import (
    TimeOfDay, DataSourceId, AircraftIdent,
    Cat007DownlinkRecord, Cat007UplinkRecord,
    Cat021Record, Cat048Record, Cat253Record,
    Cat007I250, Cat007I440, Cat021I250, Cat048I250,
    Cat253I025, Cat253I050,
    DetectionType, SimIndicator, RdpChain, SpiPresence, ReportSource,
    TestTarget, ExtendedRange, XPulsePresence, MilitaryEmergency, MilitaryId,
    FoeFriId, NatOriginValidity, AltResolution,
    CodeValidated, CodeGarbled, CodeSourceExtracted,
    Cat021NorthRef, Cat253StaleInd, Cat253LocalCtrl, Cat253DataIncl,
    Cat253Multipath, Cat253Squitter, Cat253BitReport,
)


# ── Helpers ─────────────────────────────────────────────────────────────────

def rand_u8(rng: random.Random, mx: int = 255) -> int:
    return rng.randint(0, mx)

def rand_u16(rng: random.Random, mx: int = 0xFFFF) -> int:
    return rng.randint(0, mx)

def rand_u32(rng: random.Random, mx: int = 0xFFFFFFFF) -> int:
    return rng.randint(0, mx)

def rand_i8(rng: random.Random) -> int:
    return rng.randint(-128, 127)

def rand_i16(rng: random.Random) -> int:
    return rng.randint(-32768, 32767)

def rand_bool(rng: random.Random) -> bool:
    return rng.random() < 0.5

def rand_u24(rng: random.Random) -> int:
    return rand_u32(rng, 0xFFFFFF)

def rand_signed(rng: random.Random, bits: int) -> int:
    lo = -(1 << (bits - 1))
    hi = (1 << (bits - 1)) - 1
    return rng.randint(lo, hi)

def rand_time_of_day(rng: random.Random) -> TimeOfDay:
    tod = TimeOfDay()
    tod.set_raw(rand_u32(rng, 11059200))
    return tod

def rand_data_source_id(rng: random.Random) -> DataSourceId:
    ds = DataSourceId()
    ds.set_sac(rand_u8(rng))
    ds.set_sic(rand_u8(rng))
    return ds

_ICAO_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

def rand_icao_str(rng: random.Random) -> str:
    s = "".join(rng.choice(_ICAO_CHARS) for _ in range(8))
    return s.rstrip()

def rand_aircraft_ident(rng: random.Random) -> AircraftIdent:
    ai = AircraftIdent()
    ai.set_value(rand_icao_str(rng))
    return ai


# ── RE field helpers ────────────────────────────────────────────────────────

def rand_fill_cat048_re(rng, re):
    mode5_pick = rng.randint(0, 3)
    if mode5_pick == 0:
        md5 = re.mutable_md5().mutable_sub()
        if rand_bool(rng):
            s = md5.mutable_sum()
            s.set_m5(rand_u8(rng, 1)); s.set_id(rand_u8(rng, 1))
            s.set_da(rand_u8(rng, 1)); s.set_m1(rand_u8(rng, 1))
            s.set_m2(rand_u8(rng, 1)); s.set_m3(rand_u8(rng, 1))
            s.set_mc(rand_u8(rng, 1))
        if rand_bool(rng):
            p = md5.mutable_pmn()
            p.set_pin(rand_u16(rng, 0x3FFF))
            p.set_nav(NatOriginValidity(rand_u8(rng, 1)))
            p.set_nat(rand_u8(rng, 31)); p.set_mis(rand_u8(rng, 63))
        if rand_bool(rng):
            pos = md5.mutable_pos()
            pos.set_lat(rng.uniform(-90.0, 90.0))
            pos.set_lon(rng.uniform(-180.0, 180.0))
        if rand_bool(rng):
            ga = md5.mutable_ga()
            ga.set_res(AltResolution(rand_u8(rng, 1)))
            ga.set_ga(rand_signed(rng, 14) * 25.0)
        if rand_bool(rng):
            em1 = md5.mutable_em1()
            em1.set_v(CodeValidated(rand_u8(rng, 1)))
            em1.set_g(CodeGarbled(rand_u8(rng, 1)))
            em1.set_l(CodeSourceExtracted(rand_u8(rng, 1)))
            em1.set_em1(rand_u16(rng, 0xFFF))
        if rand_bool(rng):
            md5.set_tos(rng.uniform(-1.0, 1.0))
        if rand_bool(rng):
            xp = md5.mutable_xp()
            xp.set_xp(rand_u8(rng, 1)); xp.set_x5(rand_u8(rng, 1))
            xp.set_xc(rand_u8(rng, 1)); xp.set_x3(rand_u8(rng, 1))
            xp.set_x2(rand_u8(rng, 1)); xp.set_x1(rand_u8(rng, 1))
    elif mode5_pick == 1:
        m5n = re.mutable_m5n().mutable_sub()
        if rand_bool(rng):
            s = m5n.mutable_sum()
            s.set_m5(rand_u8(rng, 1)); s.set_id(rand_u8(rng, 1))
            s.set_da(rand_u8(rng, 1)); s.set_m1(rand_u8(rng, 1))
            s.set_m2(rand_u8(rng, 1)); s.set_m3(rand_u8(rng, 1))
            s.set_mc(rand_u8(rng, 1))
        if rand_bool(rng):
            p = m5n.mutable_pmn()
            p.set_pin(rand_u16(rng, 0x3FFF))
            p.set_nov(NatOriginValidity(rand_u8(rng, 1)))
            p.set_no(rand_u16(rng, 0x7FF))
        if rand_bool(rng):
            pos = m5n.mutable_pos()
            pos.set_lat(rng.uniform(-90.0, 90.0))
            pos.set_lon(rng.uniform(-180.0, 180.0))
        if rand_bool(rng):
            ga = m5n.mutable_ga()
            ga.set_res(AltResolution(rand_u8(rng, 1)))
            ga.set_ga(rand_signed(rng, 14) * 25.0)
        if rand_bool(rng):
            em1 = m5n.mutable_em1()
            em1.set_v(CodeValidated(rand_u8(rng, 1)))
            em1.set_g(CodeGarbled(rand_u8(rng, 1)))
            em1.set_l(CodeSourceExtracted(rand_u8(rng, 1)))
            em1.set_em1(rand_u16(rng, 0xFFF))
        if rand_bool(rng):
            m5n.set_tos(rng.uniform(-1.0, 1.0))
        if rand_bool(rng):
            xp = m5n.mutable_xp()
            xp.set_xp(rand_u8(rng, 1)); xp.set_x5(rand_u8(rng, 1))
            xp.set_xc(rand_u8(rng, 1)); xp.set_x3(rand_u8(rng, 1))
            xp.set_x2(rand_u8(rng, 1)); xp.set_x1(rand_u8(rng, 1))
        if rand_bool(rng):
            m5n.mutable_fom().set_fom(rand_u8(rng, 31))

    # M4E (~50%)
    if rand_bool(rng):
        re.mutable_m4e().set_foeFri(FoeFriId(rand_u8(rng, 3)))

    # RPC (~50%)
    if rand_bool(rng):
        rpc = re.mutable_rpc().mutable_sub()
        if rand_bool(rng): rpc.set_sco(rand_u8(rng))
        if rand_bool(rng): rpc.set_scr(rand_u16(rng, 6553) * 0.1)
        if rand_bool(rng): rpc.set_rw(rand_u16(rng) * 0.00390625)
        if rand_bool(rng): rpc.set_ar(rand_u16(rng) * 0.00390625)

    # ERR (~50%)
    if rand_bool(rng):
        re.mutable_err().set_rho(rand_u24(rng) * 0.00390625)


def rand_fill_cat021_re(rng, re):
    if rand_bool(rng):
        re.mutable_bps().set_bps(rand_u16(rng, 4095) * 0.1)
    if rand_bool(rng):
        selh = re.mutable_selh()
        selh.set_hrd(Cat021NorthRef(rand_u8(rng, 1)))
        selh.set_stat(rand_u8(rng, 1))
        selh.set_selh(rng.uniform(0.0, 359.296875))
    if rand_bool(rng):
        nav = re.mutable_nav()
        nav.set_ap(rand_u8(rng, 1)); nav.set_vn(rand_u8(rng, 1))
        nav.set_ah(rand_u8(rng, 1)); nav.set_am(rand_u8(rng, 1))
    if rand_bool(rng):
        re.set_gao(rand_u8(rng))
    if rand_bool(rng):
        sgv = re.mutable_sgv()
        sgv.set_stp(rand_u8(rng, 1)); sgv.set_hts(rand_u8(rng, 1))
        sgv.set_htt(Cat021NorthRef(rand_u8(rng, 1)))
        sgv.set_hrd(Cat021NorthRef(rand_u8(rng, 1)))
        sgv.set_gss(rand_u16(rng, 2047) * 0.125)
        if rand_bool(rng):
            sgv.set_hgt(rand_u8(rng, 127) * 2.8125)
    if rand_bool(rng):
        sta = re.mutable_sta()
        sta.set_es(rand_u8(rng, 1)); sta.set_uat(rand_u8(rng, 1))
    if rand_bool(rng):
        re.set_tnh(rng.uniform(0.0, 360.0))
    # MES (~30%)
    if rand_bool(rng) and rand_bool(rng):
        mes = re.mutable_mes().mutable_sub()
        if rand_bool(rng):
            s = mes.mutable_sum()
            s.set_m5(rand_u8(rng, 1)); s.set_id(rand_u8(rng, 1))
            s.set_da(rand_u8(rng, 1)); s.set_m1(rand_u8(rng, 1))
            s.set_m2(rand_u8(rng, 1)); s.set_m3(rand_u8(rng, 1))
            s.set_mc(rand_u8(rng, 1)); s.set_po(rand_u8(rng, 1))
        if rand_bool(rng):
            pno = mes.mutable_pno()
            pno.set_pin(rand_u16(rng, 0x3FFF))
            pno.set_no(rand_u16(rng, 0x7FF))
        if rand_bool(rng):
            em1 = mes.mutable_em1()
            em1.set_v(CodeValidated(rand_u8(rng, 1)))
            em1.set_l(CodeSourceExtracted(rand_u8(rng, 1)))
            em1.set_em1(rand_u16(rng, 0xFFF))
        if rand_bool(rng):
            xp = mes.mutable_xp()
            xp.set_xp(rand_u8(rng, 1)); xp.set_x5(rand_u8(rng, 1))
            xp.set_xc(rand_u8(rng, 1)); xp.set_x3(rand_u8(rng, 1))
            xp.set_x2(rand_u8(rng, 1)); xp.set_x1(rand_u8(rng, 1))
        if rand_bool(rng):
            mes.mutable_fom().set_fom(rand_u8(rng, 31))
        if rand_bool(rng):
            m2 = mes.mutable_m2()
            m2.set_v(CodeValidated(rand_u8(rng, 1)))
            m2.set_l(CodeSourceExtracted(rand_u8(rng, 1)))
            m2.set_mode2(rand_u16(rng, 0xFFF))


# ── Array element helpers ────────────────────────────────────────────────

def rand_fill_bds(rng, i250):
    n = rng.randint(1, 3)
    i250.set_rep(n)
    for _ in range(n):
        elem = i250.new_bds_element()
        data = 0
        for _ in range(7):
            data = (data << 8) | rand_u8(rng)
        elem.set_mbData(data)
        elem.set_bds1(rand_u8(rng, 15))
        elem.set_bds2(rand_u8(rng, 15))
        i250.mutable_bds().append(elem)


def rand_fill_registers(rng, i440):
    n = rng.randint(1, 4)
    i440.set_rep(n)
    for _ in range(n):
        elem = i440.new_registers_element()
        elem.set_bds1(rand_u8(rng, 15))
        elem.set_bds2(rand_u8(rng, 15))
        i440.mutable_registers().append(elem)


def rand_fill_destinations(rng, i025):
    n = rng.randint(1, 3)
    i025.set_rep(n)
    for _ in range(n):
        elem = i025.new_destinations_element()
        elem.set_sac(rand_u8(rng))
        elem.set_sic(rand_u8(rng))
        elem.set_localId(rand_u8(rng))
        i025.mutable_destinations().append(elem)


def rand_fill_sequences(rng, i050):
    n = rng.randint(1, 4)
    i050.set_rep(n)
    for _ in range(n):
        elem = i050.new_sequences_element()
        elem.set_msid(rand_u16(rng))
        i050.mutable_sequences().append(elem)


# ── Cat007 Downlink Record ──────────────────────────────────────────────

def random_cat007_downlink(rng: random.Random) -> Cat007DownlinkRecord:
    rec = Cat007DownlinkRecord()
    it = rec.mutable_items()

    # Mandatory
    it.set_i010(rand_data_source_id(rng))
    it.set_i140(rand_time_of_day(rng))

    # Optional (~50% each)
    if rand_bool(rng): it.set_i025(rand_data_source_id(rng))
    if rand_bool(rng): it.mutable_i410()
    if rand_bool(rng): it.mutable_i400()
    if rand_bool(rng):
        i020 = it.mutable_i020()
        i020.set_typ(DetectionType(rand_u8(rng, 7)))
        i020.set_sim(SimIndicator(rand_u8(rng, 1)))
        i020.set_rdp(RdpChain(rand_u8(rng, 1)))
        i020.set_spi(SpiPresence(rand_u8(rng, 1)))
        i020.set_rab(ReportSource(rand_u8(rng, 1)))
        if rand_bool(rng):
            i020.set_tst(TestTarget(rand_u8(rng, 1)))
            i020.set_err(ExtendedRange(rand_u8(rng, 1)))
            i020.set_xpp(XPulsePresence(rand_u8(rng, 1)))
            i020.set_me(MilitaryEmergency(rand_u8(rng, 1)))
            i020.set_mi(MilitaryId(rand_u8(rng, 1)))
            i020.set_foeFri(FoeFriId(rand_u8(rng, 3)))
    if rand_bool(rng):
        i040 = it.mutable_i040()
        i040.set_rho(rng.uniform(0.0, 256.0))
        i040.set_theta(rng.uniform(0.0, 360.0))
    if rand_bool(rng): it.mutable_i070()
    if rand_bool(rng): it.mutable_i090()
    if rand_bool(rng): it.mutable_i130()
    if rand_bool(rng): it.set_i220(rand_u24(rng))
    if rand_bool(rng): it.set_i240(rand_aircraft_ident(rng))
    if rand_bool(rng): rand_fill_bds(rng, it.mutable_i250())
    if rand_bool(rng): it.mutable_i161()
    if rand_bool(rng):
        i042 = it.mutable_i042()
        i042.set_x(rng.uniform(-200.0, 200.0))
        i042.set_y(rng.uniform(-200.0, 200.0))
    if rand_bool(rng): it.mutable_i200()
    if rand_bool(rng): it.mutable_i170()
    if rand_bool(rng): it.mutable_i210()
    if rand_bool(rng): it.mutable_i030()
    if rand_bool(rng): it.mutable_i080()
    if rand_bool(rng): it.mutable_i100()
    if rand_bool(rng): it.mutable_i110()
    if rand_bool(rng): it.mutable_i120()
    if rand_bool(rng): it.mutable_i230()
    if rand_bool(rng): it.mutable_i260()
    if rand_bool(rng): it.mutable_i055()
    if rand_bool(rng): it.mutable_i050()
    if rand_bool(rng): it.mutable_i065()
    if rand_bool(rng): it.mutable_i060()
    if rand_bool(rng): it.mutable_i450()
    if rand_bool(rng): it.mutable_i085()

    return rec


# ── Cat007 Uplink Record ────────────────────────────────────────────────

def random_cat007_uplink(rng: random.Random) -> Cat007UplinkRecord:
    rec = Cat007UplinkRecord()
    it = rec.mutable_items()

    # Mandatory
    it.set_i010(rand_data_source_id(rng))
    it.set_i140(rand_time_of_day(rng))

    # Optional
    if rand_bool(rng): it.set_i025(rand_data_source_id(rng))
    if rand_bool(rng): it.mutable_i410()
    if rand_bool(rng): it.mutable_i400()
    if rand_bool(rng):
        i040 = it.mutable_i040()
        i040.set_rho(rng.uniform(0.0, 256.0))
        i040.set_theta(rng.uniform(0.0, 360.0))
    if rand_bool(rng): it.set_i220(rand_u24(rng))
    if rand_bool(rng): it.mutable_i161()
    if rand_bool(rng):
        i042 = it.mutable_i042()
        i042.set_x(rng.uniform(-200.0, 200.0))
        i042.set_y(rng.uniform(-200.0, 200.0))
    if rand_bool(rng): it.mutable_i200()
    if rand_bool(rng): it.mutable_i415()
    if rand_bool(rng): it.mutable_i420()
    if rand_bool(rng): rand_fill_registers(rng, it.mutable_i440())

    return rec


# ── Cat021 Record ───────────────────────────────────────────────────────

def random_cat021(rng: random.Random) -> Cat021Record:
    rec = Cat021Record()
    it = rec.mutable_items()

    # Mandatory
    it.set_i010(rand_data_source_id(rng))
    it.set_i071(rand_time_of_day(rng))

    # Optional
    if rand_bool(rng): it.mutable_i040()
    if rand_bool(rng): it.mutable_i161()
    if rand_bool(rng): it.set_i015(rand_u8(rng))
    if rand_bool(rng):
        i130 = it.mutable_i130()
        i130.set_latitude(rng.uniform(-90.0, 90.0))
        i130.set_longitude(rng.uniform(-180.0, 180.0))
    if rand_bool(rng): it.mutable_i131()
    if rand_bool(rng): it.set_i072(rand_time_of_day(rng))
    if rand_bool(rng): it.mutable_i150()
    if rand_bool(rng): it.mutable_i151()
    if rand_bool(rng): it.set_i080(rand_u24(rng))
    if rand_bool(rng): it.set_i073(rand_time_of_day(rng))
    if rand_bool(rng): it.mutable_i074()
    if rand_bool(rng): it.set_i075(rand_time_of_day(rng))
    if rand_bool(rng): it.mutable_i076()
    if rand_bool(rng): it.mutable_i140()
    if rand_bool(rng): it.mutable_i090()
    if rand_bool(rng): it.mutable_i210()
    if rand_bool(rng): it.mutable_i070()
    if rand_bool(rng): it.mutable_i230()
    if rand_bool(rng): it.mutable_i145()
    if rand_bool(rng): it.mutable_i152()
    if rand_bool(rng): it.mutable_i200()
    if rand_bool(rng): it.mutable_i155()
    if rand_bool(rng): it.mutable_i157()
    if rand_bool(rng): it.mutable_i160()
    if rand_bool(rng): it.mutable_i165()
    if rand_bool(rng): it.set_i077(rand_time_of_day(rng))
    if rand_bool(rng): it.set_i170(rand_aircraft_ident(rng))
    if rand_bool(rng): it.set_i020(rand_u8(rng))
    if rand_bool(rng): it.mutable_i220()
    if rand_bool(rng): it.mutable_i146()
    if rand_bool(rng): it.mutable_i148()
    if rand_bool(rng): it.mutable_i110()
    if rand_bool(rng): it.mutable_i016()
    if rand_bool(rng): it.mutable_i008()
    if rand_bool(rng): it.mutable_i271()
    if rand_bool(rng): it.set_i132(rand_i8(rng))
    if rand_bool(rng): rand_fill_bds(rng, it.mutable_i250())
    if rand_bool(rng): it.mutable_i260()
    if rand_bool(rng): it.set_i400(rand_u8(rng))
    if rand_bool(rng): it.mutable_i295()

    # RE (~50%)
    if rand_bool(rng):
        rand_fill_cat021_re(rng, it.mutable_re().mutable_items())

    return rec


# ── Cat048 Record ───────────────────────────────────────────────────────

def random_cat048(rng: random.Random) -> Cat048Record:
    rec = Cat048Record()
    it = rec.mutable_items()

    # Mandatory
    it.set_i010(rand_data_source_id(rng))
    it.set_i140(rand_time_of_day(rng))

    # Optional
    if rand_bool(rng): it.mutable_i020()
    if rand_bool(rng):
        i040 = it.mutable_i040()
        i040.set_rho(rng.uniform(0.0, 256.0))
        i040.set_theta(rng.uniform(0.0, 360.0))
    if rand_bool(rng): it.mutable_i070()
    if rand_bool(rng): it.mutable_i090()
    if rand_bool(rng): it.mutable_i130()
    if rand_bool(rng): it.set_i220(rand_u24(rng))
    if rand_bool(rng): it.set_i240(rand_aircraft_ident(rng))
    if rand_bool(rng): rand_fill_bds(rng, it.mutable_i250())
    if rand_bool(rng): it.mutable_i161()
    if rand_bool(rng):
        i042 = it.mutable_i042()
        i042.set_x(rng.uniform(-200.0, 200.0))
        i042.set_y(rng.uniform(-200.0, 200.0))
    if rand_bool(rng): it.mutable_i200()
    if rand_bool(rng): it.mutable_i170()
    if rand_bool(rng): it.mutable_i210()
    if rand_bool(rng): it.mutable_i030()
    if rand_bool(rng): it.mutable_i080()
    if rand_bool(rng): it.mutable_i100()
    if rand_bool(rng): it.mutable_i110()
    if rand_bool(rng): it.mutable_i120()
    if rand_bool(rng): it.mutable_i230()
    if rand_bool(rng): it.mutable_i260()
    if rand_bool(rng): it.mutable_i055()
    if rand_bool(rng): it.mutable_i050()
    if rand_bool(rng): it.mutable_i065()
    if rand_bool(rng): it.mutable_i060()

    # RE (~50%)
    if rand_bool(rng):
        rand_fill_cat048_re(rng, it.mutable_re().mutable_items())

    return rec


# ── Cat253 Record ───────────────────────────────────────────────────────

def random_cat253(rng: random.Random) -> Cat253Record:
    rec = Cat253Record()
    it = rec.mutable_items()

    # Mandatory
    it.set_i010(rand_data_source_id(rng))
    it.set_i070(rand_time_of_day(rng))

    # Optional
    if rand_bool(rng): it.set_i015(rand_u8(rng))
    if rand_bool(rng): rand_fill_destinations(rng, it.mutable_i025())
    if rand_bool(rng): it.set_i030(rand_u16(rng))
    if rand_bool(rng): it.mutable_i040()
    if rand_bool(rng): rand_fill_sequences(rng, it.mutable_i050())
    if rand_bool(rng): it.mutable_i035()
    if rand_bool(rng): it.mutable_i060()
    # I080 + I100 linked
    if rand_bool(rng):
        variant = rng.randint(0, 2)
        start_indices = [5, 6, 35]

        i080 = it.mutable_i080()
        i080.set_startIndex(start_indices[variant])
        i080.set_count(rand_u8(rng))
        i080.set_stale(Cat253StaleInd(1 if rand_bool(rng) else 0))
        i080.set_sim(SimIndicator(1 if rand_bool(rng) else 0))
        i080.set_localCtrl(Cat253LocalCtrl(1 if rand_bool(rng) else 0))
        i080.set_dataIncluded(Cat253DataIncl.DATA_INCLUDED)

        i100 = it.mutable_i100()
        if variant == 0:
            i100.set_payload(Cat253Multipath())
        elif variant == 1:
            i100.set_payload(Cat253Squitter())
        else:
            i100.set_payload(Cat253BitReport())
    elif rand_bool(rng):
        it.mutable_i080()
    if rand_bool(rng): it.mutable_i090()

    return rec
