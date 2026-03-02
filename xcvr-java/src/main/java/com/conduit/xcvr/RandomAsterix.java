package com.conduit.xcvr;

import asterix.*;

import java.util.Random;

/**
 * Random ASTERIX message generators for PoC testing (client perspective).
 * Each method creates a message with mandatory items always set and optional
 * items included with ~50% probability. All struct fields are properly
 * initialized so that encode() never encounters null mandatory fields.
 *
 * Uses the bgen-generated API with direct public field access.
 */
public final class RandomAsterix {

    private RandomAsterix() {}

    // -- Helpers ---------------------------------------------------------------

    static int randU8(Random rng, int max) { return rng.nextInt(max + 1); }
    static int randU8(Random rng)          { return randU8(rng, 255); }
    static int randU16(Random rng, int max) { return rng.nextInt(max + 1); }
    static int randU16(Random rng)          { return randU16(rng, 0xFFFF); }
    static long randU32(Random rng, long max) { return Math.abs(rng.nextLong()) % (max + 1); }
    static int randI8(Random rng)  { return rng.nextInt(256) - 128; }
    static boolean randBool(Random rng) { return rng.nextBoolean(); }
    static long randU24(Random rng) { return randU32(rng, 0xFFFFFF); }

    static int randSigned(Random rng, int bits) {
        int lo = -(1 << (bits - 1));
        int hi = (1 << (bits - 1)) - 1;
        return lo + rng.nextInt(hi - lo + 1);
    }

    /** Generate a roundtrip-safe unsigned 16-bit raw value for the given scale factor.
     *  Retries until (int)(long)(raw * scale / scale) == raw to avoid FP precision issues. */
    static int rtSafeU16(Random rng, double scale) {
        for (;;) {
            int raw = rng.nextInt(65536);
            if ((int)(long)(raw * scale / scale) == raw) return raw;
        }
    }

    static int rtSafeU8(Random rng, double scale) {
        for (;;) {
            int raw = rng.nextInt(256);
            if ((int)(long)(raw * scale / scale) == raw) return raw;
        }
    }

    static int rtSafeSigned(Random rng, int bits, double scale) {
        int lo = -(1 << (bits - 1));
        int range = 1 << bits;
        for (;;) {
            int raw = lo + rng.nextInt(range);
            if ((long)(raw * scale / scale) == raw) return raw;
        }
    }

    static <E extends Enum<E>> E randEnum(Random rng, Class<E> enumClass) {
        E[] vals = enumClass.getEnumConstants();
        return vals[rng.nextInt(vals.length)];
    }

    static TimeOfDay randTimeOfDay(Random rng) {
        TimeOfDay tod = new TimeOfDay();
        tod.setRaw(randU32(rng, 11059200));
        return tod;
    }

    static DataSourceId randDataSourceId(Random rng) {
        DataSourceId ds = new DataSourceId();
        ds.sac = randU8(rng);
        ds.sic = randU8(rng);
        return ds;
    }

    static String randIcaoStr(Random rng) {
        String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        StringBuilder sb = new StringBuilder(8);
        int len = 3 + rng.nextInt(6);
        for (int i = 0; i < len; i++) {
            sb.append(chars.charAt(rng.nextInt(chars.length())));
        }
        return sb.toString();
    }

    static AircraftIdent randAircraftIdent(Random rng) {
        AircraftIdent ai = new AircraftIdent();
        ai.setValue(randIcaoStr(rng));
        return ai;
    }

    // -- Item builders for types with mandatory enum/sub fields ----------------

    static Cat007I020 randCat007I020(Random rng) {
        Cat007I020 i020 = new Cat007I020();
        i020.typ = randEnum(rng, DetectionType.class);
        i020.sim = randEnum(rng, SimIndicator.class);
        i020.rdp = randEnum(rng, RdpChain.class);
        i020.spi = randEnum(rng, SpiPresence.class);
        i020.rab = randEnum(rng, ReportSource.class);
        // Note: extension fields (tst, err, xpp, me, mi, foeFri) omitted to avoid
        // byte-alignment issue in generated FX-continuation code
        return i020;
    }

    static Cat007I070 randCat007I070(Random rng) {
        Cat007I070 it = new Cat007I070();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.l = randEnum(rng, CodeSourceExtracted.class);
        it.code = randU16(rng, 0xFFF);
        return it;
    }

    static Cat007I090 randCat007I090(Random rng) {
        Cat007I090 it = new Cat007I090();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.fl = randSigned(rng, 14) * 0.25;
        return it;
    }

    static Cat007I100 randCat007I100(Random rng) {
        Cat007I100 it = new Cat007I100();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.code = randU16(rng, 0xFFF);
        it.qxi = randU8(rng, 15);
        return it;
    }

    static Cat007I050 randCat007I050(Random rng) {
        Cat007I050 it = new Cat007I050();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.l = randEnum(rng, CodeSourceSmoothed.class);
        it.code = randU16(rng, 0xFFF);
        return it;
    }

    static Cat007I055 randCat007I055(Random rng) {
        Cat007I055 it = new Cat007I055();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.l = randEnum(rng, CodeSourceSmoothed.class);
        it.code = randU8(rng, 127);
        return it;
    }

    static Cat007I170 randCat007I170(Random rng) {
        Cat007I170 it = new Cat007I170();
        it.cnf = randEnum(rng, TrackConfidence.class);
        it.rad = randEnum(rng, SensorType.class);
        it.dou = randEnum(rng, AssocConfidence.class);
        it.mah = randEnum(rng, HorizManeuver.class);
        it.cdm = randEnum(rng, ClimbDescendMode.class);
        // Note: extension fields (tre, gho, sup, tcc) omitted to avoid
        // byte-alignment issue in generated FX-continuation code
        return it;
    }

    static Cat007I230 randCat007I230(Random rng) {
        Cat007I230 it = new Cat007I230();
        it.com = randEnum(rng, CommCapability.class);
        it.stat = randEnum(rng, FlightStatus.class);
        it.si = randEnum(rng, SiIiCapability.class);
        it.mssc = randEnum(rng, ModeSServiceCap.class);
        it.arc = randEnum(rng, AltResolution.class);
        it.aic = randEnum(rng, AircraftIdCap.class);
        it.b1a = randU8(rng, 1);
        it.b1b = randU8(rng, 15);
        return it;
    }

    static Cat048I020 randCat048I020(Random rng) {
        Cat048I020 it = new Cat048I020();
        it.typ = randEnum(rng, DetectionType.class);
        it.sim = randEnum(rng, SimIndicator.class);
        it.rdp = randEnum(rng, RdpChain.class);
        it.spi = randEnum(rng, SpiPresence.class);
        it.rab = randEnum(rng, ReportSource.class);
        // Note: extension fields (tst, err, xpp, me, mi, foeFri) omitted to avoid
        // byte-alignment issue in generated FX-continuation code
        return it;
    }

    static Cat048I070 randCat048I070(Random rng) {
        Cat048I070 it = new Cat048I070();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.l = randEnum(rng, CodeSourceExtracted.class);
        it.code = randU16(rng, 0xFFF);
        return it;
    }

    static Cat048I090 randCat048I090(Random rng) {
        Cat048I090 it = new Cat048I090();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.fl = randSigned(rng, 14) * 0.25;
        return it;
    }

    static Cat048I100 randCat048I100(Random rng) {
        Cat048I100 it = new Cat048I100();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.code = randU16(rng, 0xFFF);
        it.qxi = randU8(rng, 15);
        return it;
    }

    static Cat048I050 randCat048I050(Random rng) {
        Cat048I050 it = new Cat048I050();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.l = randEnum(rng, CodeSourceSmoothed.class);
        it.code = randU16(rng, 0xFFF);
        return it;
    }

    static Cat048I055 randCat048I055(Random rng) {
        Cat048I055 it = new Cat048I055();
        it.v = randEnum(rng, CodeValidated.class);
        it.g = randEnum(rng, CodeGarbled.class);
        it.l = randEnum(rng, CodeSourceSmoothed.class);
        it.code = randU8(rng, 127);
        return it;
    }

    static Cat048I170 randCat048I170(Random rng) {
        Cat048I170 it = new Cat048I170();
        it.cnf = randEnum(rng, TrackConfidence.class);
        it.rad = randEnum(rng, SensorType.class);
        it.dou = randEnum(rng, AssocConfidence.class);
        it.mah = randEnum(rng, HorizManeuver.class);
        it.cdm = randEnum(rng, ClimbDescendMode.class);
        // Note: extension fields (tre, gho, sup, tcc) omitted to avoid
        // byte-alignment issue in generated FX-continuation code
        return it;
    }

    static Cat048I230 randCat048I230(Random rng) {
        Cat048I230 it = new Cat048I230();
        it.com = randEnum(rng, CommCapability.class);
        it.stat = randEnum(rng, FlightStatus.class);
        it.si = randEnum(rng, SiIiCapability.class);
        it.mssc = randEnum(rng, ModeSServiceCap.class);
        it.arc = randEnum(rng, AltResolution.class);
        it.aic = randEnum(rng, AircraftIdCap.class);
        it.b1a = randU8(rng, 1);
        it.b1b = randU8(rng, 15);
        return it;
    }

    static Cat021I040 randCat021I040(Random rng) {
        Cat021I040 it = new Cat021I040();
        it.atp = randEnum(rng, Cat021AddressType.class);
        it.arc = randEnum(rng, Cat021AltReportCap.class);
        it.rc = randU8(rng, 1);
        it.rab = randEnum(rng, ReportSource.class);
        return it;
    }

    static Cat021I150 randCat021I150(Random rng) {
        Cat021I150 it = new Cat021I150();
        it.im = randEnum(rng, Cat021IasMach.class);
        it.airSpeed = randU16(rng, 0x7FFF);
        return it;
    }

    static Cat021I090 randCat021I090(Random rng) {
        Cat021I090 it = new Cat021I090();
        it.nucrNacv = randU8(rng, 7);
        it.nucpNic = randU8(rng, 15);
        return it;
    }

    static Cat021I070 randCat021I070(Random rng) {
        Cat021I070 it = new Cat021I070();
        it.code = randU16(rng, 0xFFF);
        return it;
    }

    static Cat021I146 randCat021I146(Random rng) {
        Cat021I146 it = new Cat021I146();
        it.sas = randU8(rng, 1);
        it.source = randEnum(rng, Cat021AltSource.class);
        it.altitude = randSigned(rng, 13) * 25.0;
        return it;
    }

    static Cat021I148 randCat021I148(Random rng) {
        Cat021I148 it = new Cat021I148();
        it.mv = randU8(rng, 1);
        it.ah = randU8(rng, 1);
        it.am = randU8(rng, 1);
        it.altitude = randSigned(rng, 13) * 25.0;
        return it;
    }

    // -- RE field helpers (Cat048) ---------------------------------------------

    static void randFillCat048Re(Random rng, Cat048RecordItemsReItems re) {
        int mode5Pick = rng.nextInt(4);
        if (mode5Pick == 0) {
            Cat048ReMd5 md5 = new Cat048ReMd5();
            Cat048ReMd5Sub sub = new Cat048ReMd5Sub();
            md5.sub = sub;
            if (randBool(rng)) {
                Cat048ReSum sum = new Cat048ReSum();
                sum.m5 = randU8(rng, 1); sum.id = randU8(rng, 1);
                sum.da = randU8(rng, 1); sum.m1 = randU8(rng, 1);
                sum.m2 = randU8(rng, 1); sum.m3 = randU8(rng, 1);
                sum.mc = randU8(rng, 1);
                sub.sum = sum;
            }
            if (randBool(rng)) {
                Cat048ReMd5Pmn pmn = new Cat048ReMd5Pmn();
                pmn.pin = randU16(rng, 0x3FFF);
                pmn.nav = randEnum(rng, NatOriginValidity.class);
                pmn.nat = randU8(rng, 31);
                pmn.mis = randU8(rng, 63);
                sub.pmn = pmn;
            }
            if (randBool(rng)) {
                Cat048RePos pos = new Cat048RePos();
                pos.lat = rtSafeSigned(rng, 24, 0.000021) * 0.000021;
                pos.lon = rtSafeSigned(rng, 24, 0.000021) * 0.000021;
                sub.pos = pos;
            }
            if (randBool(rng)) {
                Cat048ReGa ga = new Cat048ReGa();
                ga.res = randEnum(rng, AltResolution.class);
                ga.ga = randSigned(rng, 14) * 25.000000;
                sub.ga = ga;
            }
            if (randBool(rng)) {
                Cat048ReEm1 em1 = new Cat048ReEm1();
                em1.v = randEnum(rng, CodeValidated.class);
                em1.g = randEnum(rng, CodeGarbled.class);
                em1.l = randEnum(rng, CodeSourceExtracted.class);
                em1.em1 = randU16(rng, 0xFFF);
                sub.em1 = em1;
            }
            if (randBool(rng)) sub.tos = randSigned(rng, 8) * 0.007812;
            if (randBool(rng)) {
                Cat048ReXp xp = new Cat048ReXp();
                xp.xp = randU8(rng, 1); xp.x5 = randU8(rng, 1);
                xp.xc = randU8(rng, 1); xp.x3 = randU8(rng, 1);
                xp.x2 = randU8(rng, 1); xp.x1 = randU8(rng, 1);
                sub.xp = xp;
            }
            re.md5 = md5;
        } else if (mode5Pick == 1) {
            Cat048ReM5n m5n = new Cat048ReM5n();
            Cat048ReM5nSub sub = new Cat048ReM5nSub();
            m5n.sub = sub;
            if (randBool(rng)) {
                Cat048ReSum sum = new Cat048ReSum();
                sum.m5 = randU8(rng, 1); sum.id = randU8(rng, 1);
                sum.da = randU8(rng, 1); sum.m1 = randU8(rng, 1);
                sum.m2 = randU8(rng, 1); sum.m3 = randU8(rng, 1);
                sum.mc = randU8(rng, 1);
                sub.sum = sum;
            }
            if (randBool(rng)) {
                Cat048ReM5nPmn pmn = new Cat048ReM5nPmn();
                pmn.pin = randU16(rng, 0x3FFF);
                pmn.nov = randEnum(rng, NatOriginValidity.class);
                pmn.no = randU16(rng, 0x7FF);
                sub.pmn = pmn;
            }
            if (randBool(rng)) {
                Cat048RePos pos = new Cat048RePos();
                pos.lat = rtSafeSigned(rng, 24, 0.000021) * 0.000021;
                pos.lon = rtSafeSigned(rng, 24, 0.000021) * 0.000021;
                sub.pos = pos;
            }
            if (randBool(rng)) {
                Cat048ReGa ga = new Cat048ReGa();
                ga.res = randEnum(rng, AltResolution.class);
                ga.ga = randSigned(rng, 14) * 25.000000;
                sub.ga = ga;
            }
            if (randBool(rng)) {
                Cat048ReEm1 em1 = new Cat048ReEm1();
                em1.v = randEnum(rng, CodeValidated.class);
                em1.g = randEnum(rng, CodeGarbled.class);
                em1.l = randEnum(rng, CodeSourceExtracted.class);
                em1.em1 = randU16(rng, 0xFFF);
                sub.em1 = em1;
            }
            if (randBool(rng)) sub.tos = randSigned(rng, 8) * 0.007812;
            if (randBool(rng)) {
                Cat048ReXp xp = new Cat048ReXp();
                xp.xp = randU8(rng, 1); xp.x5 = randU8(rng, 1);
                xp.xc = randU8(rng, 1); xp.x3 = randU8(rng, 1);
                xp.x2 = randU8(rng, 1); xp.x1 = randU8(rng, 1);
                sub.xp = xp;
            }
            if (randBool(rng)) {
                Cat048ReM5nFom fom = new Cat048ReM5nFom();
                fom.fom = randU8(rng, 31);
                sub.fom = fom;
            }
            re.m5n = m5n;
        }
        if (randBool(rng)) {
            Cat048ReM4e m4e = new Cat048ReM4e();
            m4e.foeFri = randEnum(rng, FoeFriId.class);
            re.m4e = m4e;
        }
        if (randBool(rng)) {
            Cat048ReRpc rpc = new Cat048ReRpc();
            Cat048ReRpcSub sub = new Cat048ReRpcSub();
            if (randBool(rng)) sub.sco = randU8(rng);
            if (randBool(rng)) sub.scr = randU16(rng) * 0.100000;
            if (randBool(rng)) sub.rw = randU16(rng) * 0.003906;
            if (randBool(rng)) sub.ar = randU16(rng) * 0.003906;
            rpc.sub = sub;
            re.rpc = rpc;
        }
        if (randBool(rng)) {
            Cat048ReErr err = new Cat048ReErr();
            err.rho = randU24(rng) * 0.003906;
            re.err = err;
        }
    }

    // -- RE field helpers (Cat021) ---------------------------------------------

    static void randFillCat021Re(Random rng, Cat021RecordItemsReItems re) {
        if (randBool(rng)) {
            Cat021ReBps bps = new Cat021ReBps();
            bps.bps = randU16(rng, 4095) * 0.1;
            re.bps = bps;
        }
        if (randBool(rng)) {
            Cat021ReSelh selh = new Cat021ReSelh();
            selh.hrd = randEnum(rng, Cat021NorthRef.class);
            selh.stat = randU8(rng, 1);
            selh.selh = randU16(rng, 1023) * 0.703125;
            re.selh = selh;
        }
        if (randBool(rng)) {
            Cat021ReNav nav = new Cat021ReNav();
            nav.ap = randU8(rng, 1); nav.vn = randU8(rng, 1);
            nav.ah = randU8(rng, 1); nav.am = randU8(rng, 1);
            re.nav = nav;
        }
        if (randBool(rng)) re.gao = randU8(rng);
        if (randBool(rng)) {
            Cat021ReSgv sgv = new Cat021ReSgv();
            sgv.stp = randU8(rng, 1); sgv.hts = randU8(rng, 1);
            sgv.htt = randEnum(rng, Cat021NorthRef.class);
            sgv.hrd = randEnum(rng, Cat021NorthRef.class);
            sgv.gss = randU16(rng, 2047) * 0.125;
            if (randBool(rng)) sgv.hgt = (double)(randU8(rng, 127) * 2.8125);
            re.sgv = sgv;
        }
        if (randBool(rng)) {
            Cat021ReSta sta = new Cat021ReSta();
            sta.es = randU8(rng, 1); sta.uat = randU8(rng, 1);
            re.sta = sta;
        }
        if (randBool(rng)) re.tnh = (double)(rtSafeU16(rng, 0.005493) * 0.005493);
        if (randBool(rng) && randBool(rng)) {
            Cat021ReMes mes = new Cat021ReMes();
            Cat021ReMesSub sub = new Cat021ReMesSub();
            mes.sub = sub;
            if (randBool(rng)) {
                Cat021ReMesSum sum = new Cat021ReMesSum();
                sum.m5 = randU8(rng, 1); sum.id = randU8(rng, 1);
                sum.da = randU8(rng, 1); sum.m1 = randU8(rng, 1);
                sum.m2 = randU8(rng, 1); sum.m3 = randU8(rng, 1);
                sum.mc = randU8(rng, 1); sum.po = randU8(rng, 1);
                sub.sum = sum;
            }
            if (randBool(rng)) {
                Cat021ReMesPno pno = new Cat021ReMesPno();
                pno.pin = randU16(rng, 0x3FFF);
                pno.no = randU16(rng, 0x7FF);
                sub.pno = pno;
            }
            if (randBool(rng)) {
                Cat021ReMesEm1 em1 = new Cat021ReMesEm1();
                em1.v = randEnum(rng, CodeValidated.class);
                em1.l = randEnum(rng, CodeSourceExtracted.class);
                em1.em1 = randU16(rng, 0xFFF);
                sub.em1 = em1;
            }
            if (randBool(rng)) {
                Cat021ReMesXp xp = new Cat021ReMesXp();
                xp.xp = randU8(rng, 1); xp.x5 = randU8(rng, 1);
                xp.xc = randU8(rng, 1); xp.x3 = randU8(rng, 1);
                xp.x2 = randU8(rng, 1); xp.x1 = randU8(rng, 1);
                sub.xp = xp;
            }
            if (randBool(rng)) {
                Cat021ReMesFom fom = new Cat021ReMesFom();
                fom.fom = randU8(rng, 31);
                sub.fom = fom;
            }
            if (randBool(rng)) {
                Cat021ReMesM2 m2 = new Cat021ReMesM2();
                m2.v = randEnum(rng, CodeValidated.class);
                m2.l = randEnum(rng, CodeSourceExtracted.class);
                m2.mode2 = randU16(rng, 0xFFF);
                sub.m2 = m2;
            }
            re.mes = mes;
        }
    }

    // -- Array element helpers ------------------------------------------------

    static void randFillBds(Random rng, Cat048I250 i250) {
        int n = 1 + rng.nextInt(3);
        for (int j = 0; j < n; j++) {
            Cat048I250Bds elem = new Cat048I250Bds();
            long data = 0;
            for (int k = 0; k < 7; k++) data = (data << 8) | randU8(rng);
            elem.mbData = data;
            elem.bds1 = randU8(rng, 15);
            elem.bds2 = randU8(rng, 15);
            i250.bds.add(elem);
        }
    }

    static void randFillBdsCat021(Random rng, Cat021I250 i250) {
        int n = 1 + rng.nextInt(3);
        for (int j = 0; j < n; j++) {
            Cat021I250Bds elem = new Cat021I250Bds();
            long data = 0;
            for (int k = 0; k < 7; k++) data = (data << 8) | randU8(rng);
            elem.mbData = data;
            elem.bds1 = randU8(rng, 15);
            elem.bds2 = randU8(rng, 15);
            i250.bds.add(elem);
        }
    }

    static void randFillBdsCat007(Random rng, Cat007I250 i250) {
        int n = 1 + rng.nextInt(3);
        for (int j = 0; j < n; j++) {
            Cat007I250Bds elem = new Cat007I250Bds();
            long data = 0;
            for (int k = 0; k < 7; k++) data = (data << 8) | randU8(rng);
            elem.mbData = data;
            elem.bds1 = randU8(rng, 15);
            elem.bds2 = randU8(rng, 15);
            i250.bds.add(elem);
        }
    }

    static void randFillRegisters(Random rng, Cat007I440 i440) {
        int n = 1 + rng.nextInt(4);
        for (int j = 0; j < n; j++) {
            Cat007I440Registers elem = new Cat007I440Registers();
            elem.bds1 = randU8(rng, 15);
            elem.bds2 = randU8(rng, 15);
            i440.registers.add(elem);
        }
    }

    static void randFillDestinations(Random rng, Cat253I025 i025) {
        int n = 1 + rng.nextInt(3);
        for (int j = 0; j < n; j++) {
            Cat253I025Destinations elem = new Cat253I025Destinations();
            elem.sac = randU8(rng);
            elem.sic = randU8(rng);
            elem.localId = randU8(rng);
            i025.destinations.add(elem);
        }
    }

    static void randFillSequences(Random rng, Cat253I050 i050) {
        int n = 1 + rng.nextInt(4);
        for (int j = 0; j < n; j++) {
            Cat253I050Sequences elem = new Cat253I050Sequences();
            elem.msid = randU16(rng);
            i050.sequences.add(elem);
        }
    }

    // -- Cat007 Downlink Record -----------------------------------------------

    public static Cat007DownlinkRecord randomCat007Downlink(Random rng) {
        Cat007DownlinkRecord rec = new Cat007DownlinkRecord();
        Cat007DownlinkRecordItems it = new Cat007DownlinkRecordItems();
        rec.items = it;

        // Mandatory
        it.i010 = randDataSourceId(rng);
        it.i140 = randTimeOfDay(rng);

        // Optional (properly initialized)
        if (randBool(rng)) it.i025 = randDataSourceId(rng);
        if (randBool(rng)) it.i020 = randCat007I020(rng);
        if (randBool(rng)) {
            Cat007I040 i040 = new Cat007I040();
            i040.rho = randU16(rng) * 0.003906;
            i040.theta = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i040 = i040;
        }
        if (randBool(rng)) it.i070 = randCat007I070(rng);
        if (randBool(rng)) it.i090 = randCat007I090(rng);
        if (randBool(rng)) it.i220 = (int) randU24(rng);
        if (randBool(rng)) it.i240 = randAircraftIdent(rng);
        if (randBool(rng)) {
            Cat007I250 i250 = new Cat007I250();
            randFillBdsCat007(rng, i250);
            it.i250 = i250;
        }
        if (randBool(rng)) {
            Cat007I161 i161 = new Cat007I161();
            i161.trackNumber = randU16(rng, 4095);
            it.i161 = i161;
        }
        if (randBool(rng)) {
            Cat007I042 i042 = new Cat007I042();
            i042.x = randSigned(rng, 16) * 0.007812;
            i042.y = randSigned(rng, 16) * 0.007812;
            it.i042 = i042;
        }
        if (randBool(rng)) {
            Cat007I200 i200 = new Cat007I200();
            i200.groundSpeed = randU16(rng) * 0.000061;
            i200.heading = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i200 = i200;
        }
        if (randBool(rng)) it.i170 = randCat007I170(rng);
        if (randBool(rng)) {
            Cat007I210 i210 = new Cat007I210();
            i210.sigmaX = randU8(rng) * 0.007812;
            i210.sigmaY = randU8(rng) * 0.007812;
            i210.sigmaV = randU8(rng) * 0.000061;
            i210.sigmaH = rtSafeU8(rng, 0.087891) * 0.087891;
            it.i210 = i210;
        }
        if (randBool(rng)) {
            Cat007I030 i030 = new Cat007I030();
            i030.we = randU8(rng, 127);
            it.i030 = i030;
        }
        if (randBool(rng)) {
            Cat007I080 i080 = new Cat007I080();
            i080.qxi = randU8(rng, 15);
            it.i080 = i080;
        }
        if (randBool(rng)) it.i100 = randCat007I100(rng);
        if (randBool(rng)) {
            Cat007I110 i110 = new Cat007I110();
            i110.height3d = randSigned(rng, 14) * 25.000000;
            it.i110 = i110;
        }
        if (randBool(rng)) it.i230 = randCat007I230(rng);
        if (randBool(rng)) {
            Cat007I260 i260 = new Cat007I260();
            long d = 0; for (int k = 0; k < 7; k++) d = (d << 8) | randU8(rng);
            i260.mbData = d;
            it.i260 = i260;
        }
        if (randBool(rng)) it.i055 = randCat007I055(rng);
        if (randBool(rng)) it.i050 = randCat007I050(rng);
        if (randBool(rng)) {
            Cat007I065 i065 = new Cat007I065();
            i065.qxi = randU8(rng, 15);
            it.i065 = i065;
        }
        if (randBool(rng)) {
            Cat007I060 i060 = new Cat007I060();
            i060.qxi = randU8(rng, 15);
            it.i060 = i060;
        }

        return rec;
    }

    // -- Cat007 Uplink Record -------------------------------------------------

    public static Cat007UplinkRecord randomCat007Uplink(Random rng) {
        Cat007UplinkRecord rec = new Cat007UplinkRecord();
        Cat007UplinkRecordItems it = new Cat007UplinkRecordItems();
        rec.items = it;

        it.i010 = randDataSourceId(rng);
        it.i140 = randTimeOfDay(rng);

        if (randBool(rng)) it.i025 = randDataSourceId(rng);
        if (randBool(rng)) {
            Cat007I040 i040 = new Cat007I040();
            i040.rho = randU16(rng) * 0.003906;
            i040.theta = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i040 = i040;
        }
        if (randBool(rng)) it.i220 = (int) randU24(rng);
        if (randBool(rng)) {
            Cat007I161 i161 = new Cat007I161();
            i161.trackNumber = randU16(rng, 4095);
            it.i161 = i161;
        }
        if (randBool(rng)) {
            Cat007I042 i042 = new Cat007I042();
            i042.x = randSigned(rng, 16) * 0.007812;
            i042.y = randSigned(rng, 16) * 0.007812;
            it.i042 = i042;
        }
        if (randBool(rng)) {
            Cat007I200 i200 = new Cat007I200();
            i200.groundSpeed = randU16(rng) * 0.000061;
            i200.heading = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i200 = i200;
        }
        if (randBool(rng)) {
            Cat007I420 i420 = new Cat007I420();
            i420.rhoStart = randU16(rng) * 0.003906;
            i420.rhoEnd = randU16(rng) * 0.003906;
            i420.thetaStart = rtSafeU16(rng, 0.005493) * 0.005493;
            i420.thetaEnd = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i420 = i420;
        }
        if (randBool(rng)) {
            Cat007I440 i440 = new Cat007I440();
            randFillRegisters(rng, i440);
            it.i440 = i440;
        }

        return rec;
    }

    // -- Cat021 Record Items --------------------------------------------------

    public static Cat021RecordItems randomCat021Items(Random rng) {
        Cat021RecordItems it = new Cat021RecordItems();

        it.i010 = randDataSourceId(rng);
        it.i071 = randTimeOfDay(rng);

        if (randBool(rng)) it.i040 = randCat021I040(rng);
        if (randBool(rng)) {
            Cat021I161 i161 = new Cat021I161();
            i161.trackNumber = randU16(rng, 4095);
            it.i161 = i161;
        }
        if (randBool(rng)) it.i015 = randU8(rng);
        if (randBool(rng)) {
            Cat021I130 i130 = new Cat021I130();
            i130.latitude = rtSafeSigned(rng, 24, 0.000021) * 0.000021;
            i130.longitude = rtSafeSigned(rng, 24, 0.000021) * 0.000021;
            it.i130 = i130;
        }
        if (randBool(rng)) it.i072 = randTimeOfDay(rng);
        if (randBool(rng)) it.i150 = randCat021I150(rng);
        if (randBool(rng)) it.i080 = (int) randU24(rng);
        if (randBool(rng)) it.i073 = randTimeOfDay(rng);
        if (randBool(rng)) it.i075 = randTimeOfDay(rng);
        if (randBool(rng)) it.i090 = randCat021I090(rng);
        if (randBool(rng)) it.i070 = randCat021I070(rng);
        if (randBool(rng)) {
            Cat021I145 i145 = new Cat021I145();
            i145.fl = randSigned(rng, 16) * 0.25;
            it.i145 = i145;
        }
        if (randBool(rng)) {
            Cat021I200 i200 = new Cat021I200();
            i200.icf = randU8(rng, 1);
            i200.lnav = randU8(rng, 1);
            i200.me = randEnum(rng, MilitaryEmergency.class);
            i200.ps = randEnum(rng, Cat021PriorityStatus.class);
            i200.ss = randEnum(rng, Cat021SurvStatus.class);
            it.i200 = i200;
        }
        if (randBool(rng)) it.i077 = randTimeOfDay(rng);
        if (randBool(rng)) it.i170 = randAircraftIdent(rng);
        if (randBool(rng)) it.i020 = randU8(rng);
        if (randBool(rng)) it.i146 = randCat021I146(rng);
        if (randBool(rng)) it.i148 = randCat021I148(rng);
        if (randBool(rng)) it.i132 = randI8(rng);
        if (randBool(rng)) {
            Cat021I250 i250 = new Cat021I250();
            randFillBdsCat021(rng, i250);
            it.i250 = i250;
        }
        if (randBool(rng)) {
            Cat021I260 i260 = new Cat021I260();
            i260.typ = randU8(rng, 31);
            i260.styp = randU8(rng, 7);
            i260.ara = randU16(rng, 16383);
            i260.rac = randU8(rng, 15);
            i260.rat = randU8(rng, 1);
            i260.mte = randU8(rng, 1);
            i260.tti = randEnum(rng, Cat021ThreatTypeInd.class);
            i260.tid = (int) randU32(rng, 67108863);
            it.i260 = i260;
        }
        if (randBool(rng)) it.i400 = randU8(rng);

        if (randBool(rng)) {
            Cat021RecordItemsRe re = new Cat021RecordItemsRe();
            Cat021RecordItemsReItems reItems = new Cat021RecordItemsReItems();
            re.items = reItems;
            randFillCat021Re(rng, reItems);
            it.re = re;
        }

        return it;
    }

    // -- Cat048 Record Items --------------------------------------------------

    public static Cat048RecordItems randomCat048Items(Random rng) {
        Cat048RecordItems it = new Cat048RecordItems();

        it.i010 = randDataSourceId(rng);
        it.i140 = randTimeOfDay(rng);

        if (randBool(rng)) it.i020 = randCat048I020(rng);
        if (randBool(rng)) {
            Cat048I040 i040 = new Cat048I040();
            i040.rho = randU16(rng) * 0.003906;
            i040.theta = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i040 = i040;
        }
        if (randBool(rng)) it.i070 = randCat048I070(rng);
        if (randBool(rng)) it.i090 = randCat048I090(rng);
        if (randBool(rng)) it.i220 = (int) randU24(rng);
        if (randBool(rng)) it.i240 = randAircraftIdent(rng);
        if (randBool(rng)) {
            Cat048I250 i250 = new Cat048I250();
            randFillBds(rng, i250);
            it.i250 = i250;
        }
        if (randBool(rng)) {
            Cat048I161 i161 = new Cat048I161();
            i161.trackNumber = randU16(rng, 4095);
            it.i161 = i161;
        }
        if (randBool(rng)) {
            Cat048I042 i042 = new Cat048I042();
            i042.x = randSigned(rng, 16) * 0.007812;
            i042.y = randSigned(rng, 16) * 0.007812;
            it.i042 = i042;
        }
        if (randBool(rng)) {
            Cat048I200 i200 = new Cat048I200();
            i200.groundSpeed = randU16(rng) * 0.000061;
            i200.heading = rtSafeU16(rng, 0.005493) * 0.005493;
            it.i200 = i200;
        }
        if (randBool(rng)) it.i170 = randCat048I170(rng);
        if (randBool(rng)) {
            Cat048I210 i210 = new Cat048I210();
            i210.sigmaX = randU8(rng) * 0.007812;
            i210.sigmaY = randU8(rng) * 0.007812;
            i210.sigmaV = randU8(rng) * 0.000061;
            i210.sigmaH = rtSafeU8(rng, 0.087891) * 0.087891;
            it.i210 = i210;
        }
        if (randBool(rng)) {
            Cat048I030 i030 = new Cat048I030();
            i030.we = randU8(rng, 127);
            it.i030 = i030;
        }
        if (randBool(rng)) {
            Cat048I080 i080 = new Cat048I080();
            i080.qxi = randU8(rng, 15);
            it.i080 = i080;
        }
        if (randBool(rng)) it.i100 = randCat048I100(rng);
        if (randBool(rng)) {
            Cat048I110 i110 = new Cat048I110();
            i110.height3d = randSigned(rng, 14) * 25.000000;
            it.i110 = i110;
        }
        if (randBool(rng)) it.i230 = randCat048I230(rng);
        if (randBool(rng)) {
            Cat048I260 i260 = new Cat048I260();
            long d = 0; for (int k = 0; k < 7; k++) d = (d << 8) | randU8(rng);
            i260.mbData = d;
            it.i260 = i260;
        }
        if (randBool(rng)) it.i055 = randCat048I055(rng);
        if (randBool(rng)) it.i050 = randCat048I050(rng);
        if (randBool(rng)) {
            Cat048I065 i065 = new Cat048I065();
            i065.qxi = randU8(rng, 15);
            it.i065 = i065;
        }
        if (randBool(rng)) {
            Cat048I060 i060 = new Cat048I060();
            i060.qxi = randU8(rng, 15);
            it.i060 = i060;
        }

        if (randBool(rng)) {
            Cat048RecordItemsRe re = new Cat048RecordItemsRe();
            Cat048RecordItemsReItems reItems = new Cat048RecordItemsReItems();
            re.items = reItems;
            randFillCat048Re(rng, reItems);
            it.re = re;
        }

        return it;
    }

    // -- Cat253 Record --------------------------------------------------------

    public static Cat253Record randomCat253(Random rng) {
        Cat253Record rec = new Cat253Record();
        Cat253RecordItems it = new Cat253RecordItems();
        rec.items = it;

        it.i010 = randDataSourceId(rng);
        it.i070 = randTimeOfDay(rng);

        if (randBool(rng)) it.i015 = randU8(rng);
        if (randBool(rng)) {
            Cat253I025 i025 = new Cat253I025();
            randFillDestinations(rng, i025);
            it.i025 = i025;
        }
        if (randBool(rng)) it.i030 = randU16(rng);
        if (randBool(rng)) it.i040 = new Cat253I040();
        if (randBool(rng)) {
            Cat253I050 i050 = new Cat253I050();
            randFillSequences(rng, i050);
            it.i050 = i050;
        }
        if (randBool(rng)) it.i035 = new Cat253I035();
        if (randBool(rng)) it.i060 = new Cat253I060();
        // I080 + I100 are linked: I100 choice dispatches on I080's start-index
        if (randBool(rng)) {
            int variant = rng.nextInt(3);
            int[] startIndices = {5, 6, 35};

            Cat253I080 i080 = new Cat253I080();
            i080.startIndex = startIndices[variant];
            i080.count = randU8(rng);
            i080.stale = randEnum(rng, Cat253StaleInd.class);
            i080.sim = randEnum(rng, SimIndicator.class);
            i080.localCtrl = randEnum(rng, Cat253LocalCtrl.class);
            i080.dataIncluded = Cat253DataIncl.DATA_INCLUDED;
            it.i080 = i080;

            Cat253RecordItemsI100 i100 = new Cat253RecordItemsI100();
            switch (variant) {
                case 0: i100.payload = new Cat253Multipath(); break;
                case 1: i100.payload = new Cat253Squitter(); break;
                case 2: i100.payload = new Cat253BitReport(); break;
            }
            it.i100 = i100;
        } else if (randBool(rng)) {
            it.i080 = new Cat253I080();
        }
        if (randBool(rng)) it.i090 = new Cat253I090();

        return rec;
    }
}
