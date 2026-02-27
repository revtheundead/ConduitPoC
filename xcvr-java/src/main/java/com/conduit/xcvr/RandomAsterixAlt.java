package com.conduit.xcvr;

import conduit.generated.asterix_alt.*;

import java.util.Random;

/**
 * Random ASTERIX message generators for the server perspective (asterix-alt).
 * Identical logic to RandomAsterix but uses the asterix_alt generated classes
 * where Cat007DownlinkRecord is send-only and Cat007UplinkRecord is receive-only.
 *
 * Mirrors xcvr/src/random_asterix_alt.hpp
 */
public final class RandomAsterixAlt {

    private RandomAsterixAlt() {}

    // ── Helpers ─────────────────────────────────────────────────────────────

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

    static TimeOfDay randTimeOfDay(Random rng) {
        TimeOfDay tod = new TimeOfDay();
        tod.setRaw(randU32(rng, 11059200));
        return tod;
    }

    static DataSourceId randDataSourceId(Random rng) {
        DataSourceId ds = new DataSourceId();
        ds.setSac(randU8(rng));
        ds.setSic(randU8(rng));
        return ds;
    }

    static String randIcaoStr(Random rng) {
        String chars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        StringBuilder sb = new StringBuilder(8);
        for (int i = 0; i < 8; i++) {
            sb.append(chars.charAt(rng.nextInt(chars.length())));
        }
        String s = sb.toString();
        while (s.endsWith(" ")) s = s.substring(0, s.length() - 1);
        return s;
    }

    static AircraftIdent randAircraftIdent(Random rng) {
        AircraftIdent ai = new AircraftIdent();
        ai.setValue(randIcaoStr(rng));
        return ai;
    }

    // ── RE field helpers ────────────────────────────────────────────────────

    static void randFillCat048Re(Random rng, Cat048Record.Items.ReItems re) {
        int mode5Pick = rng.nextInt(4);
        if (mode5Pick == 0) {
            var md5 = re.mutableMd5().mutableSub();
            if (randBool(rng)) {
                var sum = md5.mutableSum();
                sum.setM5(randU8(rng, 1)); sum.setId(randU8(rng, 1));
                sum.setDa(randU8(rng, 1)); sum.setM1(randU8(rng, 1));
                sum.setM2(randU8(rng, 1)); sum.setM3(randU8(rng, 1));
                sum.setMc(randU8(rng, 1));
            }
            if (randBool(rng)) {
                var pmn = md5.mutablePmn();
                pmn.setPin(randU16(rng, 0x3FFF));
                pmn.setNav(NatOriginValidity.fromValue(randU8(rng, 1)));
                pmn.setNat(randU8(rng, 31));
                pmn.setMis(randU8(rng, 63));
            }
            if (randBool(rng)) {
                var pos = md5.mutablePos();
                pos.setLat(-90.0 + rng.nextDouble() * 180.0);
                pos.setLon(-180.0 + rng.nextDouble() * 360.0);
            }
            if (randBool(rng)) {
                var ga = md5.mutableGa();
                ga.setRes(AltResolution.fromValue(randU8(rng, 1)));
                ga.setGa(randSigned(rng, 14) * 25.0);
            }
            if (randBool(rng)) {
                var em1 = md5.mutableEm1();
                em1.setV(CodeValidated.fromValue(randU8(rng, 1)));
                em1.setG(CodeGarbled.fromValue(randU8(rng, 1)));
                em1.setL(CodeSourceExtracted.fromValue(randU8(rng, 1)));
                em1.setEm1(randU16(rng, 0xFFF));
            }
            if (randBool(rng)) md5.setTos(-1.0 + rng.nextDouble() * 2.0);
            if (randBool(rng)) {
                var xp = md5.mutableXp();
                xp.setXp(randU8(rng, 1)); xp.setX5(randU8(rng, 1));
                xp.setXc(randU8(rng, 1)); xp.setX3(randU8(rng, 1));
                xp.setX2(randU8(rng, 1)); xp.setX1(randU8(rng, 1));
            }
        } else if (mode5Pick == 1) {
            var m5n = re.mutableM5n().mutableSub();
            if (randBool(rng)) {
                var sum = m5n.mutableSum();
                sum.setM5(randU8(rng, 1)); sum.setId(randU8(rng, 1));
                sum.setDa(randU8(rng, 1)); sum.setM1(randU8(rng, 1));
                sum.setM2(randU8(rng, 1)); sum.setM3(randU8(rng, 1));
                sum.setMc(randU8(rng, 1));
            }
            if (randBool(rng)) {
                var pmn = m5n.mutablePmn();
                pmn.setPin(randU16(rng, 0x3FFF));
                pmn.setNov(NatOriginValidity.fromValue(randU8(rng, 1)));
                pmn.setNo(randU16(rng, 0x7FF));
            }
            if (randBool(rng)) {
                var pos = m5n.mutablePos();
                pos.setLat(-90.0 + rng.nextDouble() * 180.0);
                pos.setLon(-180.0 + rng.nextDouble() * 360.0);
            }
            if (randBool(rng)) {
                var ga = m5n.mutableGa();
                ga.setRes(AltResolution.fromValue(randU8(rng, 1)));
                ga.setGa(randSigned(rng, 14) * 25.0);
            }
            if (randBool(rng)) {
                var em1 = m5n.mutableEm1();
                em1.setV(CodeValidated.fromValue(randU8(rng, 1)));
                em1.setG(CodeGarbled.fromValue(randU8(rng, 1)));
                em1.setL(CodeSourceExtracted.fromValue(randU8(rng, 1)));
                em1.setEm1(randU16(rng, 0xFFF));
            }
            if (randBool(rng)) m5n.setTos(-1.0 + rng.nextDouble() * 2.0);
            if (randBool(rng)) {
                var xp = m5n.mutableXp();
                xp.setXp(randU8(rng, 1)); xp.setX5(randU8(rng, 1));
                xp.setXc(randU8(rng, 1)); xp.setX3(randU8(rng, 1));
                xp.setX2(randU8(rng, 1)); xp.setX1(randU8(rng, 1));
            }
            if (randBool(rng)) m5n.mutableFom().setFom(randU8(rng, 31));
        }

        if (randBool(rng)) {
            re.mutableM4e().setFoeFri(FoeFriId.fromValue(randU8(rng, 3)));
        }
        if (randBool(rng)) {
            var rpc = re.mutableRpc().mutableSub();
            if (randBool(rng)) rpc.setSco(randU8(rng));
            if (randBool(rng)) rpc.setScr(randU16(rng, 6553) * 0.1);
            if (randBool(rng)) rpc.setRw(randU16(rng) * 0.00390625);
            if (randBool(rng)) rpc.setAr(randU16(rng) * 0.00390625);
        }
        if (randBool(rng)) {
            re.mutableErr().setRho(randU24(rng) * 0.00390625);
        }
    }

    static void randFillCat021Re(Random rng, Cat021Record.Items.ReItems re) {
        if (randBool(rng)) re.mutableBps().setBps(randU16(rng, 4095) * 0.1);
        if (randBool(rng)) {
            var selh = re.mutableSelh();
            selh.setHrd(Cat021NorthRef.fromValue(randU8(rng, 1)));
            selh.setStat(randU8(rng, 1));
            selh.setSelh(rng.nextDouble() * 359.296875);
        }
        if (randBool(rng)) {
            var nav = re.mutableNav();
            nav.setAp(randU8(rng, 1)); nav.setVn(randU8(rng, 1));
            nav.setAh(randU8(rng, 1)); nav.setAm(randU8(rng, 1));
        }
        if (randBool(rng)) re.setGao(randU8(rng));
        if (randBool(rng)) {
            var sgv = re.mutableSgv();
            sgv.setStp(randU8(rng, 1)); sgv.setHts(randU8(rng, 1));
            sgv.setHtt(Cat021NorthRef.fromValue(randU8(rng, 1)));
            sgv.setHrd(Cat021NorthRef.fromValue(randU8(rng, 1)));
            sgv.setGss(randU16(rng, 2047) * 0.125);
            if (randBool(rng)) sgv.setHgt(randU8(rng, 127) * 2.8125);
        }
        if (randBool(rng)) {
            var sta = re.mutableSta();
            sta.setEs(randU8(rng, 1)); sta.setUat(randU8(rng, 1));
        }
        if (randBool(rng)) re.setTnh(rng.nextDouble() * 360.0);
        if (randBool(rng) && randBool(rng)) {
            var mes = re.mutableMes().mutableSub();
            if (randBool(rng)) {
                var sum = mes.mutableSum();
                sum.setM5(randU8(rng, 1)); sum.setId(randU8(rng, 1));
                sum.setDa(randU8(rng, 1)); sum.setM1(randU8(rng, 1));
                sum.setM2(randU8(rng, 1)); sum.setM3(randU8(rng, 1));
                sum.setMc(randU8(rng, 1)); sum.setPo(randU8(rng, 1));
            }
            if (randBool(rng)) {
                var pno = mes.mutablePno();
                pno.setPin(randU16(rng, 0x3FFF));
                pno.setNo(randU16(rng, 0x7FF));
            }
            if (randBool(rng)) {
                var em1 = mes.mutableEm1();
                em1.setV(CodeValidated.fromValue(randU8(rng, 1)));
                em1.setL(CodeSourceExtracted.fromValue(randU8(rng, 1)));
                em1.setEm1(randU16(rng, 0xFFF));
            }
            if (randBool(rng)) {
                var xp = mes.mutableXp();
                xp.setXp(randU8(rng, 1)); xp.setX5(randU8(rng, 1));
                xp.setXc(randU8(rng, 1)); xp.setX3(randU8(rng, 1));
                xp.setX2(randU8(rng, 1)); xp.setX1(randU8(rng, 1));
            }
            if (randBool(rng)) mes.mutableFom().setFom(randU8(rng, 31));
            if (randBool(rng)) {
                var m2 = mes.mutableM2();
                m2.setV(CodeValidated.fromValue(randU8(rng, 1)));
                m2.setL(CodeSourceExtracted.fromValue(randU8(rng, 1)));
                m2.setMode2(randU16(rng, 0xFFF));
            }
        }
    }

    // ── Array element helpers ────────────────────────────────────────────────

    static void randFillBds(Random rng, Cat048I250 i250) {
        int n = 1 + rng.nextInt(3);
        i250.setRep(n);
        for (int j = 0; j < n; j++) {
            var elem = new Cat048I250.BdsElement();
            long data = 0;
            for (int k = 0; k < 7; k++) data = (data << 8) | randU8(rng);
            elem.setMbData(data);
            elem.setBds1(randU8(rng, 15));
            elem.setBds2(randU8(rng, 15));
            i250.mutableBds().add(elem);
        }
    }

    static void randFillBdsCat021(Random rng, Cat021I250 i250) {
        int n = 1 + rng.nextInt(3);
        i250.setRep(n);
        for (int j = 0; j < n; j++) {
            var elem = new Cat021I250.BdsElement();
            long data = 0;
            for (int k = 0; k < 7; k++) data = (data << 8) | randU8(rng);
            elem.setMbData(data);
            elem.setBds1(randU8(rng, 15));
            elem.setBds2(randU8(rng, 15));
            i250.mutableBds().add(elem);
        }
    }

    static void randFillBdsCat007(Random rng, Cat007I250 i250) {
        int n = 1 + rng.nextInt(3);
        i250.setRep(n);
        for (int j = 0; j < n; j++) {
            var elem = new Cat007I250.BdsElement();
            long data = 0;
            for (int k = 0; k < 7; k++) data = (data << 8) | randU8(rng);
            elem.setMbData(data);
            elem.setBds1(randU8(rng, 15));
            elem.setBds2(randU8(rng, 15));
            i250.mutableBds().add(elem);
        }
    }

    static void randFillDestinations(Random rng, Cat253I025 i025) {
        int n = 1 + rng.nextInt(3);
        i025.setRep(n);
        for (int j = 0; j < n; j++) {
            var elem = new Cat253I025.DestinationsElement();
            elem.setSac(randU8(rng));
            elem.setSic(randU8(rng));
            elem.setLocalId(randU8(rng));
            i025.mutableDestinations().add(elem);
        }
    }

    static void randFillSequences(Random rng, Cat253I050 i050) {
        int n = 1 + rng.nextInt(4);
        i050.setRep(n);
        for (int j = 0; j < n; j++) {
            var elem = new Cat253I050.SequencesElement();
            elem.setMsid(randU16(rng));
            i050.mutableSequences().add(elem);
        }
    }

    // ── Cat007 Downlink Record (server sends this) ──────────────────────────

    public static Cat007DownlinkRecord randomCat007Downlink(Random rng) {
        Cat007DownlinkRecord rec = new Cat007DownlinkRecord();
        var it = rec.mutableItems();

        it.setI010(randDataSourceId(rng));
        it.setI140(randTimeOfDay(rng));

        if (randBool(rng)) it.setI025(randDataSourceId(rng));
        if (randBool(rng)) it.mutableI410();
        if (randBool(rng)) it.mutableI400();
        if (randBool(rng)) {
            var i020 = it.mutableI020();
            i020.setTyp(DetectionType.fromValue(randU8(rng, 7)));
            i020.setSim(SimIndicator.fromValue(randU8(rng, 1)));
            i020.setRdp(RdpChain.fromValue(randU8(rng, 1)));
            i020.setSpi(SpiPresence.fromValue(randU8(rng, 1)));
            i020.setRab(ReportSource.fromValue(randU8(rng, 1)));
            if (randBool(rng)) {
                i020.setTst(TestTarget.fromValue(randU8(rng, 1)));
                i020.setErr(ExtendedRange.fromValue(randU8(rng, 1)));
                i020.setXpp(XPulsePresence.fromValue(randU8(rng, 1)));
                i020.setMe(MilitaryEmergency.fromValue(randU8(rng, 1)));
                i020.setMi(MilitaryId.fromValue(randU8(rng, 1)));
                i020.setFoeFri(FoeFriId.fromValue(randU8(rng, 3)));
            }
        }
        if (randBool(rng)) {
            var i040 = it.mutableI040();
            i040.setRho(rng.nextDouble() * 256.0);
            i040.setTheta(rng.nextDouble() * 360.0);
        }
        if (randBool(rng)) it.mutableI070();
        if (randBool(rng)) it.mutableI090();
        if (randBool(rng)) it.mutableI130();
        if (randBool(rng)) it.setI220(randU24(rng));
        if (randBool(rng)) it.setI240(randAircraftIdent(rng));
        if (randBool(rng)) randFillBdsCat007(rng, it.mutableI250());
        if (randBool(rng)) it.mutableI161();
        if (randBool(rng)) {
            var i042 = it.mutableI042();
            i042.setX(-200.0 + rng.nextDouble() * 400.0);
            i042.setY(-200.0 + rng.nextDouble() * 400.0);
        }
        if (randBool(rng)) it.mutableI200();
        if (randBool(rng)) it.mutableI170();
        if (randBool(rng)) it.mutableI210();
        if (randBool(rng)) it.mutableI030();
        if (randBool(rng)) it.mutableI080();
        if (randBool(rng)) it.mutableI100();
        if (randBool(rng)) it.mutableI110();
        if (randBool(rng)) it.mutableI120();
        if (randBool(rng)) it.mutableI230();
        if (randBool(rng)) it.mutableI260();
        if (randBool(rng)) it.mutableI055();
        if (randBool(rng)) it.mutableI050();
        if (randBool(rng)) it.mutableI065();
        if (randBool(rng)) it.mutableI060();
        if (randBool(rng)) it.mutableI450();
        if (randBool(rng)) it.mutableI085();

        return rec;
    }

    // ── Cat021 Record ───────────────────────────────────────────────────────

    public static Cat021Record randomCat021(Random rng) {
        Cat021Record rec = new Cat021Record();
        var it = rec.mutableItems();

        it.setI010(randDataSourceId(rng));
        it.setI071(randTimeOfDay(rng));

        if (randBool(rng)) it.mutableI040();
        if (randBool(rng)) it.mutableI161();
        if (randBool(rng)) it.setI015(randU8(rng));
        if (randBool(rng)) {
            var i130 = it.mutableI130();
            i130.setLatitude(-90.0 + rng.nextDouble() * 180.0);
            i130.setLongitude(-180.0 + rng.nextDouble() * 360.0);
        }
        if (randBool(rng)) it.mutableI131();
        if (randBool(rng)) it.setI072(randTimeOfDay(rng));
        if (randBool(rng)) it.mutableI150();
        if (randBool(rng)) it.mutableI151();
        if (randBool(rng)) it.setI080(randU24(rng));
        if (randBool(rng)) it.setI073(randTimeOfDay(rng));
        if (randBool(rng)) it.mutableI074();
        if (randBool(rng)) it.setI075(randTimeOfDay(rng));
        if (randBool(rng)) it.mutableI076();
        if (randBool(rng)) it.mutableI140();
        if (randBool(rng)) it.mutableI090();
        if (randBool(rng)) it.mutableI210();
        if (randBool(rng)) it.mutableI070();
        if (randBool(rng)) it.mutableI230();
        if (randBool(rng)) it.mutableI145();
        if (randBool(rng)) it.mutableI152();
        if (randBool(rng)) it.mutableI200();
        if (randBool(rng)) it.mutableI155();
        if (randBool(rng)) it.mutableI157();
        if (randBool(rng)) it.mutableI160();
        if (randBool(rng)) it.mutableI165();
        if (randBool(rng)) it.setI077(randTimeOfDay(rng));
        if (randBool(rng)) it.setI170(randAircraftIdent(rng));
        if (randBool(rng)) it.setI020(randU8(rng));
        if (randBool(rng)) it.mutableI220();
        if (randBool(rng)) it.mutableI146();
        if (randBool(rng)) it.mutableI148();
        if (randBool(rng)) it.mutableI110();
        if (randBool(rng)) it.mutableI016();
        if (randBool(rng)) it.mutableI008();
        if (randBool(rng)) it.mutableI271();
        if (randBool(rng)) it.setI132(randI8(rng));
        if (randBool(rng)) randFillBdsCat021(rng, it.mutableI250());
        if (randBool(rng)) it.mutableI260();
        if (randBool(rng)) it.setI400(randU8(rng));
        if (randBool(rng)) it.mutableI295();

        if (randBool(rng)) {
            randFillCat021Re(rng, it.mutableRe().mutableItems());
        }

        return rec;
    }

    // ── Cat048 Record ───────────────────────────────────────────────────────

    public static Cat048Record randomCat048(Random rng) {
        Cat048Record rec = new Cat048Record();
        var it = rec.mutableItems();

        it.setI010(randDataSourceId(rng));
        it.setI140(randTimeOfDay(rng));

        if (randBool(rng)) it.mutableI020();
        if (randBool(rng)) {
            var i040 = it.mutableI040();
            i040.setRho(rng.nextDouble() * 256.0);
            i040.setTheta(rng.nextDouble() * 360.0);
        }
        if (randBool(rng)) it.mutableI070();
        if (randBool(rng)) it.mutableI090();
        if (randBool(rng)) it.mutableI130();
        if (randBool(rng)) it.setI220(randU24(rng));
        if (randBool(rng)) it.setI240(randAircraftIdent(rng));
        if (randBool(rng)) randFillBds(rng, it.mutableI250());
        if (randBool(rng)) it.mutableI161();
        if (randBool(rng)) {
            var i042 = it.mutableI042();
            i042.setX(-200.0 + rng.nextDouble() * 400.0);
            i042.setY(-200.0 + rng.nextDouble() * 400.0);
        }
        if (randBool(rng)) it.mutableI200();
        if (randBool(rng)) it.mutableI170();
        if (randBool(rng)) it.mutableI210();
        if (randBool(rng)) it.mutableI030();
        if (randBool(rng)) it.mutableI080();
        if (randBool(rng)) it.mutableI100();
        if (randBool(rng)) it.mutableI110();
        if (randBool(rng)) it.mutableI120();
        if (randBool(rng)) it.mutableI230();
        if (randBool(rng)) it.mutableI260();
        if (randBool(rng)) it.mutableI055();
        if (randBool(rng)) it.mutableI050();
        if (randBool(rng)) it.mutableI065();
        if (randBool(rng)) it.mutableI060();

        if (randBool(rng)) {
            randFillCat048Re(rng, it.mutableRe().mutableItems());
        }

        return rec;
    }

    // ── Cat253 Record ───────────────────────────────────────────────────────

    public static Cat253Record randomCat253(Random rng) {
        Cat253Record rec = new Cat253Record();
        var it = rec.mutableItems();

        it.setI010(randDataSourceId(rng));
        it.setI070(randTimeOfDay(rng));

        if (randBool(rng)) it.setI015(randU8(rng));
        if (randBool(rng)) randFillDestinations(rng, it.mutableI025());
        if (randBool(rng)) it.setI030(randU16(rng));
        if (randBool(rng)) it.mutableI040();
        if (randBool(rng)) randFillSequences(rng, it.mutableI050());
        if (randBool(rng)) it.mutableI035();
        if (randBool(rng)) it.mutableI060();
        if (randBool(rng)) {
            int variant = rng.nextInt(3);
            int[] startIndices = {5, 6, 35};

            var i080 = it.mutableI080();
            i080.setStartIndex(startIndices[variant]);
            i080.setCount(randU8(rng));
            i080.setStale(Cat253StaleInd.fromValue(randBool(rng) ? 1 : 0));
            i080.setSim(SimIndicator.fromValue(randBool(rng) ? 1 : 0));
            i080.setLocalCtrl(Cat253LocalCtrl.fromValue(randBool(rng) ? 1 : 0));
            i080.setDataIncluded(Cat253DataIncl.DATA_INCLUDED);

            var i100 = it.mutableI100();
            switch (variant) {
                case 0 -> i100.setPayload(new Cat253Multipath());
                case 1 -> i100.setPayload(new Cat253Squitter());
                case 2 -> i100.setPayload(new Cat253BitReport());
            }
        } else if (randBool(rng)) {
            it.mutableI080();
        }
        if (randBool(rng)) it.mutableI090();

        return rec;
    }
}
