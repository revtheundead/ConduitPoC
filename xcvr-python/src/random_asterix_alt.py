"""Random ASTERIX message generators for the server perspective (asterix-alt).

Identical logic to random_asterix.py but uses the asterix_alt generated classes
where Cat007DownlinkRecord is send-only and Cat007UplinkRecord is receive-only.

Uses the bgen-generated pure Python API with direct attribute access.
"""

import sys
import os
import random
import importlib
import importlib.util

# ---------------------------------------------------------------------------
# Load the asterix-alt generated package under the 'generated_alt' namespace.
# This avoids collisions with the 'generated' package already loaded from the
# asterix (client) path which uses camelCase field names.
# ---------------------------------------------------------------------------
_ALT_DIR = os.path.join(os.path.dirname(__file__), '..', 'asterix-alt')
_GEN_DIR = os.path.join(_ALT_DIR, 'generated')
_PKG = 'generated_alt'

if _PKG not in sys.modules:
    _init_spec = importlib.util.spec_from_file_location(
        _PKG, os.path.join(_GEN_DIR, '__init__.py'),
        submodule_search_locations=[_GEN_DIR])
    _pkg_mod = importlib.util.module_from_spec(_init_spec)
    sys.modules[_PKG] = _pkg_mod
    _init_spec.loader.exec_module(_pkg_mod)

    for _sub in ('bit_io', 'constants', 'types', 'structs', 'protocol',
                 'sessions', 'messages'):
        _fpath = os.path.join(_GEN_DIR, _sub + '.py')
        if not os.path.exists(_fpath):
            continue
        _sub_spec = importlib.util.spec_from_file_location(
            f'{_PKG}.{_sub}', _fpath)
        _sub_mod = importlib.util.module_from_spec(_sub_spec)
        sys.modules[f'{_PKG}.{_sub}'] = _sub_mod
        _sub_spec.loader.exec_module(_sub_mod)

from generated_alt.messages import (
    Cat007DownlinkRecord, Cat007DownlinkRecordItems,
    Cat021Record, Cat021RecordItems,
    Cat021RecordItemsRe, Cat021RecordItemsReItems,
    Cat048Record, Cat048RecordItems,
    Cat048RecordItemsRe, Cat048RecordItemsReItems,
    Cat253Record, Cat253RecordItems, Cat253RecordItemsI100,
)
from generated_alt.structs import (
    DataSourceId,
    Cat007I020, Cat007I040, Cat007I042, Cat007I070, Cat007I090,
    Cat007I161, Cat007I200, Cat007I210, Cat007I250, Cat007I250Bds,
    Cat007I030, Cat007I080, Cat007I100, Cat007I110,
    Cat007I230, Cat007I260, Cat007I055, Cat007I050, Cat007I065, Cat007I060,
    Cat007I170,
    Cat021I040, Cat021I070, Cat021I090, Cat021I130,
    Cat021I145, Cat021I146, Cat021I148, Cat021I150, Cat021I161,
    Cat021I200, Cat021I250, Cat021I250Bds, Cat021I260,
    Cat048I020, Cat048I040, Cat048I042, Cat048I070, Cat048I090,
    Cat048I161, Cat048I200, Cat048I210, Cat048I250, Cat048I250Bds,
    Cat048I030, Cat048I080, Cat048I100, Cat048I110,
    Cat048I230, Cat048I260, Cat048I055, Cat048I050, Cat048I065, Cat048I060,
    Cat048I170,
    Cat048ReMd5, Cat048ReMd5Sub, Cat048ReMd5Pmn,
    Cat048ReM5n, Cat048ReM5nSub, Cat048ReM5nPmn, Cat048ReM5nFom,
    Cat048ReM4e, Cat048ReRpc, Cat048ReRpcSub, Cat048ReErr,
    Cat048ReSum, Cat048RePos, Cat048ReGa, Cat048ReEm1, Cat048ReXp,
    Cat021ReBps, Cat021ReSelh, Cat021ReNav, Cat021ReSgv, Cat021ReSta,
    Cat021ReMes, Cat021ReMesSub, Cat021ReMesSum, Cat021ReMesPno,
    Cat021ReMesEm1, Cat021ReMesXp, Cat021ReMesFom, Cat021ReMesM2,
    Cat253I025, Cat253I025Destinations,
    Cat253I050, Cat253I050Sequences,
    Cat253I080,
    Cat253Multipath, Cat253Squitter, Cat253BitReport,
)
from generated_alt.types import (
    TimeOfDay, AircraftIdent,
    DetectionType, SimIndicator, RdpChain, SpiPresence, ReportSource,
    NatOriginValidity, AltResolution,
    CodeValidated, CodeGarbled, CodeSourceExtracted, CodeSourceSmoothed,
    TrackConfidence, SensorType, AssocConfidence, HorizManeuver, ClimbDescendMode,
    CommCapability, FlightStatus, SiIiCapability, ModeSServiceCap, AircraftIdCap,
    FoeFriId,
    Cat021AddressType, Cat021AltReportCap, Cat021IasMach, Cat021AltSource,
    Cat021NorthRef, Cat021PriorityStatus, Cat021SurvStatus, MilitaryEmergency,
    Cat021ThreatTypeInd,
    Cat253StaleInd, Cat253LocalCtrl, Cat253DataIncl,
)


# -- Helpers (identical to random_asterix) ----------------------------------

def rand_u8(rng, mx=255):
    return rng.randint(0, mx)

def rand_u16(rng, mx=0xFFFF):
    return rng.randint(0, mx)

def rand_u32(rng, mx=0xFFFFFFFF):
    return rng.randint(0, mx)

def rand_i8(rng):
    return rng.randint(-128, 127)

def rand_bool(rng):
    return rng.random() < 0.5

def rand_u24(rng):
    return rand_u32(rng, 0xFFFFFF)

def rand_signed(rng, bits):
    lo = -(1 << (bits - 1))
    hi = (1 << (bits - 1)) - 1
    return rng.randint(lo, hi)

def rt_safe_u16(rng, scale):
    while True:
        raw = rng.randint(0, 65535)
        if int(raw * scale / scale) == raw:
            return raw

def rt_safe_signed(rng, bits, scale):
    lo = -(1 << (bits - 1))
    hi = (1 << (bits - 1)) - 1
    while True:
        raw = rng.randint(lo, hi)
        if int(raw * scale / scale) == raw:
            return raw

def rand_enum(rng, enum_cls):
    vals = list(enum_cls)
    return rng.choice(vals)

def rand_time_of_day(rng):
    return TimeOfDay(rand_u32(rng, 11059200))

def rand_data_source_id(rng):
    ds = DataSourceId()
    ds.sac = rand_u8(rng)
    ds.sic = rand_u8(rng)
    return ds

_ICAO_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

def rand_icao_str(rng):
    s = "".join(rng.choice(_ICAO_CHARS) for _ in range(8))
    return s.rstrip()

def rand_aircraft_ident(rng):
    return AircraftIdent(rand_icao_str(rng))


# -- Item builders ---------------------------------------------------------

def rand_cat007_i070(rng):
    it = Cat007I070()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.l = rand_enum(rng, CodeSourceExtracted)
    it.code = rand_u16(rng, 0xFFF)
    return it

def rand_cat007_i090(rng):
    it = Cat007I090()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.fl = rand_signed(rng, 14) * 0.25
    return it

def rand_cat007_i100(rng):
    it = Cat007I100()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.code = rand_u16(rng, 0xFFF)
    it.qxi = rand_u8(rng, 15)
    return it

def rand_cat007_i050(rng):
    it = Cat007I050()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.l = rand_enum(rng, CodeSourceSmoothed)
    it.code = rand_u16(rng, 0xFFF)
    return it

def rand_cat007_i055(rng):
    it = Cat007I055()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.l = rand_enum(rng, CodeSourceSmoothed)
    it.code = rand_u8(rng, 127)
    return it

def rand_cat007_i170(rng):
    it = Cat007I170()
    it.cnf = rand_enum(rng, TrackConfidence)
    it.rad = rand_enum(rng, SensorType)
    it.dou = rand_enum(rng, AssocConfidence)
    it.mah = rand_enum(rng, HorizManeuver)
    it.cdm = rand_enum(rng, ClimbDescendMode)
    return it

def rand_cat007_i230(rng):
    it = Cat007I230()
    it.com = rand_enum(rng, CommCapability)
    it.stat = rand_enum(rng, FlightStatus)
    it.si = rand_enum(rng, SiIiCapability)
    it.mssc = rand_enum(rng, ModeSServiceCap)
    it.arc = rand_enum(rng, AltResolution)
    it.aic = rand_enum(rng, AircraftIdCap)
    it.b1a = rand_u8(rng, 1)
    it.b1b = rand_u8(rng, 15)
    return it

def rand_cat048_i020(rng):
    it = Cat048I020()
    it.typ = rand_enum(rng, DetectionType)
    it.sim = rand_enum(rng, SimIndicator)
    it.rdp = rand_enum(rng, RdpChain)
    it.spi = rand_enum(rng, SpiPresence)
    it.rab = rand_enum(rng, ReportSource)
    return it

def rand_cat048_i070(rng):
    it = Cat048I070()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.l = rand_enum(rng, CodeSourceExtracted)
    it.code = rand_u16(rng, 0xFFF)
    return it

def rand_cat048_i090(rng):
    it = Cat048I090()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.fl = rand_signed(rng, 14) * 0.25
    return it

def rand_cat048_i100(rng):
    it = Cat048I100()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.code = rand_u16(rng, 0xFFF)
    it.qxi = rand_u8(rng, 15)
    return it

def rand_cat048_i050(rng):
    it = Cat048I050()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.l = rand_enum(rng, CodeSourceSmoothed)
    it.code = rand_u16(rng, 0xFFF)
    return it

def rand_cat048_i055(rng):
    it = Cat048I055()
    it.v = rand_enum(rng, CodeValidated)
    it.g = rand_enum(rng, CodeGarbled)
    it.l = rand_enum(rng, CodeSourceSmoothed)
    it.code = rand_u8(rng, 127)
    return it

def rand_cat048_i170(rng):
    it = Cat048I170()
    it.cnf = rand_enum(rng, TrackConfidence)
    it.rad = rand_enum(rng, SensorType)
    it.dou = rand_enum(rng, AssocConfidence)
    it.mah = rand_enum(rng, HorizManeuver)
    it.cdm = rand_enum(rng, ClimbDescendMode)
    return it

def rand_cat048_i230(rng):
    it = Cat048I230()
    it.com = rand_enum(rng, CommCapability)
    it.stat = rand_enum(rng, FlightStatus)
    it.si = rand_enum(rng, SiIiCapability)
    it.mssc = rand_enum(rng, ModeSServiceCap)
    it.arc = rand_enum(rng, AltResolution)
    it.aic = rand_enum(rng, AircraftIdCap)
    it.b1a = rand_u8(rng, 1)
    it.b1b = rand_u8(rng, 15)
    return it

def rand_cat021_i040(rng):
    it = Cat021I040()
    it.atp = rand_enum(rng, Cat021AddressType)
    it.arc = rand_enum(rng, Cat021AltReportCap)
    it.rc = rand_u8(rng, 1)
    it.rab = rand_enum(rng, ReportSource)
    return it

def rand_cat021_i150(rng):
    it = Cat021I150()
    it.im = rand_enum(rng, Cat021IasMach)
    it.air_speed = rand_u16(rng, 0x7FFF)
    return it

def rand_cat021_i090(rng):
    it = Cat021I090()
    it.nucr_nacv = rand_u8(rng, 7)
    it.nucp_nic = rand_u8(rng, 15)
    return it

def rand_cat021_i070(rng):
    it = Cat021I070()
    it.code = rand_u16(rng, 0xFFF)
    return it

def rand_cat021_i146(rng):
    it = Cat021I146()
    it.sas = rand_u8(rng, 1)
    it.source = rand_enum(rng, Cat021AltSource)
    it.altitude = rand_signed(rng, 13) * 25.0
    return it

def rand_cat021_i148(rng):
    it = Cat021I148()
    it.mv = rand_u8(rng, 1)
    it.ah = rand_u8(rng, 1)
    it.am = rand_u8(rng, 1)
    it.altitude = rand_signed(rng, 13) * 25.0
    return it


# -- RE field helpers (Cat048) ---------------------------------------------

def rand_fill_cat048_re(rng, re_items):
    mode5_pick = rng.randint(0, 3)
    if mode5_pick == 0:
        md5 = Cat048ReMd5()
        sub = Cat048ReMd5Sub()
        if rand_bool(rng):
            s = Cat048ReSum()
            s.m5 = rand_u8(rng, 1); s.id = rand_u8(rng, 1)
            s.da = rand_u8(rng, 1); s.m1 = rand_u8(rng, 1)
            s.m2 = rand_u8(rng, 1); s.m3 = rand_u8(rng, 1)
            s.mc = rand_u8(rng, 1)
            sub.sum = s
        if rand_bool(rng):
            p = Cat048ReMd5Pmn()
            p.pin = rand_u16(rng, 0x3FFF)
            p.nav = rand_enum(rng, NatOriginValidity)
            p.nat = rand_u8(rng, 31); p.mis = rand_u8(rng, 63)
            sub.pmn = p
        if rand_bool(rng):
            pos = Cat048RePos()
            pos.lat = rt_safe_signed(rng, 24, 0.000021) * 0.000021
            pos.lon = rt_safe_signed(rng, 24, 0.000021) * 0.000021
            sub.pos = pos
        if rand_bool(rng):
            ga = Cat048ReGa()
            ga.res = rand_enum(rng, AltResolution)
            ga.ga = rand_signed(rng, 14) * 25.0
            sub.ga = ga
        if rand_bool(rng):
            em1 = Cat048ReEm1()
            em1.v = rand_enum(rng, CodeValidated)
            em1.g = rand_enum(rng, CodeGarbled)
            em1.l = rand_enum(rng, CodeSourceExtracted)
            em1.em1 = rand_u16(rng, 0xFFF)
            sub.em1 = em1
        if rand_bool(rng):
            sub.tos = rand_signed(rng, 8) * 0.007812
        if rand_bool(rng):
            xp = Cat048ReXp()
            xp.xp = rand_u8(rng, 1); xp.x5 = rand_u8(rng, 1)
            xp.xc = rand_u8(rng, 1); xp.x3 = rand_u8(rng, 1)
            xp.x2 = rand_u8(rng, 1); xp.x1 = rand_u8(rng, 1)
            sub.xp = xp
        md5.sub = sub
        re_items.md5 = md5
    elif mode5_pick == 1:
        m5n = Cat048ReM5n()
        sub = Cat048ReM5nSub()
        if rand_bool(rng):
            s = Cat048ReSum()
            s.m5 = rand_u8(rng, 1); s.id = rand_u8(rng, 1)
            s.da = rand_u8(rng, 1); s.m1 = rand_u8(rng, 1)
            s.m2 = rand_u8(rng, 1); s.m3 = rand_u8(rng, 1)
            s.mc = rand_u8(rng, 1)
            sub.sum = s
        if rand_bool(rng):
            p = Cat048ReM5nPmn()
            p.pin = rand_u16(rng, 0x3FFF)
            p.nov = rand_enum(rng, NatOriginValidity)
            p.no = rand_u16(rng, 0x7FF)
            sub.pmn = p
        if rand_bool(rng):
            pos = Cat048RePos()
            pos.lat = rt_safe_signed(rng, 24, 0.000021) * 0.000021
            pos.lon = rt_safe_signed(rng, 24, 0.000021) * 0.000021
            sub.pos = pos
        if rand_bool(rng):
            ga = Cat048ReGa()
            ga.res = rand_enum(rng, AltResolution)
            ga.ga = rand_signed(rng, 14) * 25.0
            sub.ga = ga
        if rand_bool(rng):
            em1 = Cat048ReEm1()
            em1.v = rand_enum(rng, CodeValidated)
            em1.g = rand_enum(rng, CodeGarbled)
            em1.l = rand_enum(rng, CodeSourceExtracted)
            em1.em1 = rand_u16(rng, 0xFFF)
            sub.em1 = em1
        if rand_bool(rng):
            sub.tos = rand_signed(rng, 8) * 0.007812
        if rand_bool(rng):
            xp = Cat048ReXp()
            xp.xp = rand_u8(rng, 1); xp.x5 = rand_u8(rng, 1)
            xp.xc = rand_u8(rng, 1); xp.x3 = rand_u8(rng, 1)
            xp.x2 = rand_u8(rng, 1); xp.x1 = rand_u8(rng, 1)
            sub.xp = xp
        if rand_bool(rng):
            fom = Cat048ReM5nFom()
            fom.fom = rand_u8(rng, 31)
            sub.fom = fom
        m5n.sub = sub
        re_items.m5n = m5n
    if rand_bool(rng):
        m4e = Cat048ReM4e()
        m4e.foe_fri = rand_enum(rng, FoeFriId)
        re_items.m4e = m4e
    if rand_bool(rng):
        rpc = Cat048ReRpc()
        sub = Cat048ReRpcSub()
        if rand_bool(rng): sub.sco = rand_u8(rng)
        if rand_bool(rng): sub.scr = rand_u16(rng) * 0.100000
        if rand_bool(rng): sub.rw = rand_u16(rng) * 0.003906
        if rand_bool(rng): sub.ar = rand_u16(rng) * 0.003906
        rpc.sub = sub
        re_items.rpc = rpc
    if rand_bool(rng):
        err = Cat048ReErr()
        err.rho = rand_u24(rng) * 0.00390625
        re_items.err = err


# -- RE field helpers (Cat021) ---------------------------------------------

def rand_fill_cat021_re(rng, re_items):
    if rand_bool(rng):
        bps = Cat021ReBps()
        bps.bps = rand_u16(rng, 4095) * 0.1
        re_items.bps = bps
    if rand_bool(rng):
        selh = Cat021ReSelh()
        selh.hrd = rand_enum(rng, Cat021NorthRef)
        selh.stat = rand_u8(rng, 1)
        selh.selh = rand_u16(rng, 1023) * 0.703125
        re_items.selh = selh
    if rand_bool(rng):
        nav = Cat021ReNav()
        nav.ap = rand_u8(rng, 1); nav.vn = rand_u8(rng, 1)
        nav.ah = rand_u8(rng, 1); nav.am = rand_u8(rng, 1)
        re_items.nav = nav
    if rand_bool(rng):
        re_items.gao = rand_u8(rng)
    if rand_bool(rng):
        sgv = Cat021ReSgv()
        sgv.stp = rand_u8(rng, 1); sgv.hts = rand_u8(rng, 1)
        sgv.htt = rand_enum(rng, Cat021NorthRef)
        sgv.hrd = rand_enum(rng, Cat021NorthRef)
        sgv.gss = rand_u16(rng, 2047) * 0.125
        if rand_bool(rng):
            sgv.hgt = rand_u8(rng, 127) * 2.8125
        re_items.sgv = sgv
    if rand_bool(rng):
        sta = Cat021ReSta()
        sta.es = rand_u8(rng, 1); sta.uat = rand_u8(rng, 1)
        re_items.sta = sta
    if rand_bool(rng):
        re_items.tnh = rt_safe_u16(rng, 0.005493) * 0.005493
    if rand_bool(rng) and rand_bool(rng):
        mes = Cat021ReMes()
        sub = Cat021ReMesSub()
        if rand_bool(rng):
            s = Cat021ReMesSum()
            s.m5 = rand_u8(rng, 1); s.id = rand_u8(rng, 1)
            s.da = rand_u8(rng, 1); s.m1 = rand_u8(rng, 1)
            s.m2 = rand_u8(rng, 1); s.m3 = rand_u8(rng, 1)
            s.mc = rand_u8(rng, 1); s.po = rand_u8(rng, 1)
            sub.sum = s
        if rand_bool(rng):
            pno = Cat021ReMesPno()
            pno.pin = rand_u16(rng, 0x3FFF)
            pno.no = rand_u16(rng, 0x7FF)
            sub.pno = pno
        if rand_bool(rng):
            em1 = Cat021ReMesEm1()
            em1.v = rand_enum(rng, CodeValidated)
            em1.l = rand_enum(rng, CodeSourceExtracted)
            em1.em1 = rand_u16(rng, 0xFFF)
            sub.em1 = em1
        if rand_bool(rng):
            xp = Cat021ReMesXp()
            xp.xp = rand_u8(rng, 1); xp.x5 = rand_u8(rng, 1)
            xp.xc = rand_u8(rng, 1); xp.x3 = rand_u8(rng, 1)
            xp.x2 = rand_u8(rng, 1); xp.x1 = rand_u8(rng, 1)
            sub.xp = xp
        if rand_bool(rng):
            fom = Cat021ReMesFom()
            fom.fom = rand_u8(rng, 31)
            sub.fom = fom
        if rand_bool(rng):
            m2 = Cat021ReMesM2()
            m2.v = rand_enum(rng, CodeValidated)
            m2.l = rand_enum(rng, CodeSourceExtracted)
            m2.mode2 = rand_u16(rng, 0xFFF)
            sub.m2 = m2
        mes.sub = sub
        re_items.mes = mes


# -- Array element helpers -------------------------------------------------

def rand_fill_bds_cat007(rng, i250):
    n = rng.randint(1, 3)
    i250.rep = n
    for _ in range(n):
        elem = Cat007I250Bds()
        data = 0
        for _ in range(7):
            data = (data << 8) | rand_u8(rng)
        elem.mb_data = data
        elem.bds1 = rand_u8(rng, 15)
        elem.bds2 = rand_u8(rng, 15)
        i250.bds.append(elem)

def rand_fill_bds_cat021(rng, i250):
    n = rng.randint(1, 3)
    i250.rep = n
    for _ in range(n):
        elem = Cat021I250Bds()
        data = 0
        for _ in range(7):
            data = (data << 8) | rand_u8(rng)
        elem.mb_data = data
        elem.bds1 = rand_u8(rng, 15)
        elem.bds2 = rand_u8(rng, 15)
        i250.bds.append(elem)

def rand_fill_bds_cat048(rng, i250):
    n = rng.randint(1, 3)
    i250.rep = n
    for _ in range(n):
        elem = Cat048I250Bds()
        data = 0
        for _ in range(7):
            data = (data << 8) | rand_u8(rng)
        elem.mb_data = data
        elem.bds1 = rand_u8(rng, 15)
        elem.bds2 = rand_u8(rng, 15)
        i250.bds.append(elem)

def rand_fill_destinations(rng, i025):
    n = rng.randint(1, 3)
    i025.rep = n
    for _ in range(n):
        elem = Cat253I025Destinations()
        elem.sac = rand_u8(rng)
        elem.sic = rand_u8(rng)
        elem.local_id = rand_u8(rng)
        i025.destinations.append(elem)

def rand_fill_sequences(rng, i050):
    n = rng.randint(1, 4)
    i050.rep = n
    for _ in range(n):
        elem = Cat253I050Sequences()
        elem.msid = rand_u16(rng)
        i050.sequences.append(elem)


# -- Cat007 Downlink Record (server sends this) ----------------------------

def random_cat007_downlink(rng):
    rec = Cat007DownlinkRecord()
    it = Cat007DownlinkRecordItems()
    rec.items = it

    it.i010 = rand_data_source_id(rng)
    it.i140 = rand_time_of_day(rng)

    if rand_bool(rng): it.i025 = rand_data_source_id(rng)
    if rand_bool(rng):
        i020 = Cat007I020()
        i020.typ = rand_enum(rng, DetectionType)
        i020.sim = rand_enum(rng, SimIndicator)
        i020.rdp = rand_enum(rng, RdpChain)
        i020.spi = rand_enum(rng, SpiPresence)
        i020.rab = rand_enum(rng, ReportSource)
        it.i020 = i020
    if rand_bool(rng):
        i040 = Cat007I040()
        i040.rho = rand_u16(rng) * 0.00390625
        i040.theta = rand_u16(rng) * 0.0054931640625
        it.i040 = i040
    if rand_bool(rng): it.i070 = rand_cat007_i070(rng)
    if rand_bool(rng): it.i090 = rand_cat007_i090(rng)
    if rand_bool(rng): it.i220 = rand_u24(rng)
    if rand_bool(rng): it.i240 = rand_aircraft_ident(rng)
    if rand_bool(rng):
        i250 = Cat007I250()
        rand_fill_bds_cat007(rng, i250)
        it.i250 = i250
    if rand_bool(rng):
        i161 = Cat007I161()
        i161.track_number = rand_u16(rng, 4095)
        it.i161 = i161
    if rand_bool(rng):
        i042 = Cat007I042()
        i042.x = rand_signed(rng, 16) * 0.0078125
        i042.y = rand_signed(rng, 16) * 0.0078125
        it.i042 = i042
    if rand_bool(rng):
        i200 = Cat007I200()
        i200.ground_speed = rand_u16(rng) * 0.000061
        i200.heading = rand_u16(rng) * 0.0054931640625
        it.i200 = i200
    if rand_bool(rng): it.i170 = rand_cat007_i170(rng)
    if rand_bool(rng):
        i210 = Cat007I210()
        i210.sigma_x = rand_u8(rng) * 0.0078125
        i210.sigma_y = rand_u8(rng) * 0.0078125
        i210.sigma_v = rand_u8(rng) * 0.000061
        i210.sigma_h = rand_u8(rng) * 0.087890625
        it.i210 = i210
    if rand_bool(rng):
        i030 = Cat007I030()
        i030.we = rand_u8(rng, 127)
        it.i030 = i030
    if rand_bool(rng):
        i080 = Cat007I080()
        i080.qxi = rand_u8(rng, 15)
        it.i080 = i080
    if rand_bool(rng): it.i100 = rand_cat007_i100(rng)
    if rand_bool(rng):
        i110 = Cat007I110()
        i110.height_3d = rand_signed(rng, 14) * 25.0
        it.i110 = i110
    if rand_bool(rng): it.i230 = rand_cat007_i230(rng)
    if rand_bool(rng):
        i260 = Cat007I260()
        d = 0
        for _ in range(7): d = (d << 8) | rand_u8(rng)
        i260.mb_data = d
        it.i260 = i260
    if rand_bool(rng): it.i055 = rand_cat007_i055(rng)
    if rand_bool(rng): it.i050 = rand_cat007_i050(rng)
    if rand_bool(rng):
        i065 = Cat007I065()
        i065.qxi = rand_u8(rng, 15)
        it.i065 = i065
    if rand_bool(rng):
        i060 = Cat007I060()
        i060.qxi = rand_u8(rng, 15)
        it.i060 = i060

    return rec


# -- Cat021 Record --------------------------------------------------------

def random_cat021(rng):
    rec = Cat021Record()
    it = Cat021RecordItems()
    rec.items = it

    it.i010 = rand_data_source_id(rng)
    it.i071 = rand_time_of_day(rng)

    if rand_bool(rng): it.i040 = rand_cat021_i040(rng)
    if rand_bool(rng):
        i161 = Cat021I161()
        i161.track_number = rand_u16(rng, 4095)
        it.i161 = i161
    if rand_bool(rng): it.i015 = rand_u8(rng)
    if rand_bool(rng):
        i130 = Cat021I130()
        i130.latitude = rt_safe_signed(rng, 24, 0.000021) * 0.000021
        i130.longitude = rt_safe_signed(rng, 24, 0.000021) * 0.000021
        it.i130 = i130
    if rand_bool(rng): it.i072 = rand_time_of_day(rng)
    if rand_bool(rng): it.i150 = rand_cat021_i150(rng)
    if rand_bool(rng): it.i080 = rand_u24(rng)
    if rand_bool(rng): it.i073 = rand_time_of_day(rng)
    if rand_bool(rng): it.i075 = rand_time_of_day(rng)
    if rand_bool(rng): it.i090 = rand_cat021_i090(rng)
    if rand_bool(rng): it.i070 = rand_cat021_i070(rng)
    if rand_bool(rng):
        i145 = Cat021I145()
        i145.fl = rand_signed(rng, 16) * 0.25
        it.i145 = i145
    if rand_bool(rng):
        i200 = Cat021I200()
        i200.icf = rand_u8(rng, 1)
        i200.lnav = rand_u8(rng, 1)
        i200.me = rand_enum(rng, MilitaryEmergency)
        i200.ps = rand_enum(rng, Cat021PriorityStatus)
        i200.ss = rand_enum(rng, Cat021SurvStatus)
        it.i200 = i200
    if rand_bool(rng): it.i077 = rand_time_of_day(rng)
    if rand_bool(rng): it.i170 = rand_aircraft_ident(rng)
    if rand_bool(rng): it.i020 = rand_u8(rng)
    if rand_bool(rng): it.i146 = rand_cat021_i146(rng)
    if rand_bool(rng): it.i148 = rand_cat021_i148(rng)
    if rand_bool(rng): it.i132 = rand_i8(rng)
    if rand_bool(rng):
        i250 = Cat021I250()
        rand_fill_bds_cat021(rng, i250)
        it.i250 = i250
    if rand_bool(rng):
        i260 = Cat021I260()
        i260.typ = rand_u8(rng, 31)
        i260.styp = rand_u8(rng, 7)
        i260.ara = rand_u16(rng, 16383)
        i260.rac = rand_u8(rng, 15)
        i260.rat = rand_u8(rng, 1)
        i260.mte = rand_u8(rng, 1)
        i260.tti = rand_enum(rng, Cat021ThreatTypeInd)
        i260.tid = rand_u32(rng, 67108863)
        it.i260 = i260
    if rand_bool(rng): it.i400 = rand_u8(rng)

    if rand_bool(rng):
        re_outer = Cat021RecordItemsRe()
        re_items = Cat021RecordItemsReItems()
        rand_fill_cat021_re(rng, re_items)
        re_outer.items = re_items
        it.re = re_outer

    return rec


# -- Cat048 Record --------------------------------------------------------

def random_cat048(rng):
    rec = Cat048Record()
    it = Cat048RecordItems()
    rec.items = it

    it.i010 = rand_data_source_id(rng)
    it.i140 = rand_time_of_day(rng)

    if rand_bool(rng): it.i020 = rand_cat048_i020(rng)
    if rand_bool(rng):
        i040 = Cat048I040()
        i040.rho = rand_u16(rng) * 0.00390625
        i040.theta = rand_u16(rng) * 0.0054931640625
        it.i040 = i040
    if rand_bool(rng): it.i070 = rand_cat048_i070(rng)
    if rand_bool(rng): it.i090 = rand_cat048_i090(rng)
    if rand_bool(rng): it.i220 = rand_u24(rng)
    if rand_bool(rng): it.i240 = rand_aircraft_ident(rng)
    if rand_bool(rng):
        i250 = Cat048I250()
        rand_fill_bds_cat048(rng, i250)
        it.i250 = i250
    if rand_bool(rng):
        i161 = Cat048I161()
        i161.track_number = rand_u16(rng, 4095)
        it.i161 = i161
    if rand_bool(rng):
        i042 = Cat048I042()
        i042.x = rand_signed(rng, 16) * 0.0078125
        i042.y = rand_signed(rng, 16) * 0.0078125
        it.i042 = i042
    if rand_bool(rng):
        i200 = Cat048I200()
        i200.ground_speed = rand_u16(rng) * 0.000061
        i200.heading = rand_u16(rng) * 0.0054931640625
        it.i200 = i200
    if rand_bool(rng): it.i170 = rand_cat048_i170(rng)
    if rand_bool(rng):
        i210 = Cat048I210()
        i210.sigma_x = rand_u8(rng) * 0.0078125
        i210.sigma_y = rand_u8(rng) * 0.0078125
        i210.sigma_v = rand_u8(rng) * 0.000061
        i210.sigma_h = rand_u8(rng) * 0.087890625
        it.i210 = i210
    if rand_bool(rng):
        i030 = Cat048I030()
        i030.we = rand_u8(rng, 127)
        it.i030 = i030
    if rand_bool(rng):
        i080 = Cat048I080()
        i080.qxi = rand_u8(rng, 15)
        it.i080 = i080
    if rand_bool(rng): it.i100 = rand_cat048_i100(rng)
    if rand_bool(rng):
        i110 = Cat048I110()
        i110.height_3d = rand_signed(rng, 14) * 25.0
        it.i110 = i110
    if rand_bool(rng): it.i230 = rand_cat048_i230(rng)
    if rand_bool(rng):
        i260 = Cat048I260()
        d = 0
        for _ in range(7): d = (d << 8) | rand_u8(rng)
        i260.mb_data = d
        it.i260 = i260
    if rand_bool(rng): it.i055 = rand_cat048_i055(rng)
    if rand_bool(rng): it.i050 = rand_cat048_i050(rng)
    if rand_bool(rng):
        i065 = Cat048I065()
        i065.qxi = rand_u8(rng, 15)
        it.i065 = i065
    if rand_bool(rng):
        i060 = Cat048I060()
        i060.qxi = rand_u8(rng, 15)
        it.i060 = i060

    if rand_bool(rng):
        re_outer = Cat048RecordItemsRe()
        re_items = Cat048RecordItemsReItems()
        rand_fill_cat048_re(rng, re_items)
        re_outer.items = re_items
        it.re = re_outer

    return rec


# -- Cat253 Record --------------------------------------------------------

def random_cat253(rng):
    rec = Cat253Record()
    it = Cat253RecordItems()
    rec.items = it

    it.i010 = rand_data_source_id(rng)
    it.i070 = rand_time_of_day(rng)

    if rand_bool(rng): it.i015 = rand_u8(rng)
    if rand_bool(rng):
        i025 = Cat253I025()
        rand_fill_destinations(rng, i025)
        it.i025 = i025
    if rand_bool(rng): it.i030 = rand_u16(rng)
    if rand_bool(rng):
        i050 = Cat253I050()
        rand_fill_sequences(rng, i050)
        it.i050 = i050
    # I080 + I100 are linked: I100 choice dispatches on I080's start-index
    if rand_bool(rng):
        variant = rng.randint(0, 2)
        start_indices = [5, 6, 35]

        i080 = Cat253I080()
        i080.start_index = start_indices[variant]
        i080.count = rand_u8(rng)
        i080.stale = rand_enum(rng, Cat253StaleInd)
        i080.sim = rand_enum(rng, SimIndicator)
        i080.local_ctrl = rand_enum(rng, Cat253LocalCtrl)
        i080.data_included = Cat253DataIncl.DATA_INCLUDED
        it.i080 = i080

        i100 = Cat253RecordItemsI100()
        if variant == 0:
            i100.payload = Cat253Multipath()
        elif variant == 1:
            i100.payload = Cat253Squitter()
        else:
            i100.payload = Cat253BitReport()
        it.i100 = i100
    elif rand_bool(rng):
        i080 = Cat253I080()
        i080.start_index = 0
        i080.count = 0
        i080.stale = Cat253StaleInd.CURRENT
        i080.sim = SimIndicator.ACTUAL
        i080.local_ctrl = Cat253LocalCtrl.NOT_LOCAL
        i080.data_included = Cat253DataIncl.NO_DATA
        it.i080 = i080

    return rec
