// SPDX-License-Identifier: MIT
// Shared wire data builder for Java benchmarks.
// Uses bgen-generated Java code to construct realistic messages and encode
// them to wire bytes, matching the C++ benchmark message factories exactly.

public final class BenchWireData {

    public static final int IDX_HEARTBEAT = 0;
    public static final int IDX_ALERT = 1;
    public static final int IDX_SENSOR = 2;
    public static final int IDX_CONFIG = 3;
    public static final int IDX_PING = 4;
    public static final int IDX_TELEMETRY = 5;
    public static final int IDX_SURV_TYP = 6;
    public static final int IDX_SURV_MAX = 7;
    public static final int COUNT = 8;

    public static final String[] LABELS = {
        "sentry Heartbeat", "sentry Alert", "sentry Sensor", "sentry Config",
        "stress Ping", "stress Telemetry", "stress Surveillance typical",
        "stress Surveillance maximal"
    };

    public static byte[][] buildAll() {
        byte[][] wire = new byte[COUNT][];
        wire[IDX_HEARTBEAT] = encodeSentry(makeHeartbeat());
        wire[IDX_ALERT] = encodeSentry(makeAlert());
        wire[IDX_SENSOR] = encodeSentry(makeSensor());
        wire[IDX_CONFIG] = encodeSentry(makeConfig());
        wire[IDX_PING] = encodeStress(makePing());
        wire[IDX_TELEMETRY] = encodeStress(makeTelemetry());
        wire[IDX_SURV_TYP] = encodeStress(makeSurveillanceTypical());
        wire[IDX_SURV_MAX] = encodeStress(makeSurveillanceMaximal());
        return wire;
    }

    // ================================================================
    // Sentry Link
    // ================================================================

    static sentry_link.HeartbeatBody makeHeartbeat() {
        sentry_link.HeartbeatBody hb = new sentry_link.HeartbeatBody();
        hb.timestamp = 1700000000;
        hb.uptimeHours = 720;
        hb.status = sentry_link.DeviceStatus.ONLINE;
        hb.cpuLoad = 65;
        return hb;
    }

    static sentry_link.AlertBody makeAlert() {
        sentry_link.AlertBody ab = new sentry_link.AlertBody();
        ab.timestamp = 1700000100;
        ab.sourceId = 42;
        ab.severity = sentry_link.SeverityLevel.WARNING;
        ab.category = 5;
        ab.alertCode = 1001;
        ab.message = "Sensor temperature exceeds limit";
        return ab;
    }

    static sentry_link.SensorBody makeSensor() {
        sentry_link.SensorBody sb = new sentry_link.SensorBody();
        sb.sensorId = 101;
        sb.timestamp = 1700000200;
        sentry_link.SensorFlags flags = new sentry_link.SensorFlags();
        flags.channel = 3;
        flags.precision = 2;
        flags.saturated = 0;
        flags.valid = 1;
        sb.flags = flags;
        sb.rawValue = 23.45;
        sb.unitCode = 1;
        return sb;
    }

    static sentry_link.ConfigBody makeConfig() {
        sentry_link.ConfigBody cb = new sentry_link.ConfigBody();
        cb.deviceName = "sensor-node-01";
        sentry_link.FirmwareVersion fw = new sentry_link.FirmwareVersion();
        fw.major = 2;
        fw.minor = 5;
        fw.patch = 1024;
        cb.firmware = fw;
        cb.mode = sentry_link.DeviceMode.ACTIVE;
        cb.logLevel = 3;
        cb.autoReport = 1;
        cb.compression = 0;
        cb.sampleRate = 1000;
        return cb;
    }

    static byte[] encodeSentry(Object msg) {
        sentry_link.Frame frame = null;
        if (msg instanceof sentry_link.HeartbeatBody)
            frame = sentry_link.Frame.wrap((sentry_link.HeartbeatBody) msg);
        else if (msg instanceof sentry_link.AlertBody)
            frame = sentry_link.Frame.wrap((sentry_link.AlertBody) msg);
        else if (msg instanceof sentry_link.SensorBody)
            frame = sentry_link.Frame.wrap((sentry_link.SensorBody) msg);
        else if (msg instanceof sentry_link.ConfigBody)
            frame = sentry_link.Frame.wrap((sentry_link.ConfigBody) msg);
        else
            throw new IllegalArgumentException("Unknown sentry_link message type");
        return frame.encodeBytes();
    }

    // ================================================================
    // Stress protocol
    // ================================================================

    static stress.Ping makePing() {
        stress.Ping p = new stress.Ping();
        p.sequence = 12345;
        p.timestamp = 1700000000;
        p.priority = stress.PriorityLevel.HIGH;
        stress.StatusFlags f = new stress.StatusFlags();
        f.setActive(true);
        f.setCalibrated(true);
        f.setGpsLock(true);
        p.flags = f;
        p.tag = 0xBEEF;
        return p;
    }

    static stress.TelemetryReport makeTelemetry() {
        stress.TelemetryReport tr = new stress.TelemetryReport();
        tr.sequence = 67890;
        tr.timestamp = 1700001000;
        tr.sourceId = 42;
        tr.sensor = stress.SensorType.ACCELERATION;
        stress.Coordinate pos = new stress.Coordinate();
        pos.x = 100000;
        pos.y = -200000;
        pos.z = 5000;
        tr.position = pos;
        stress.Velocity vel = new stress.Velocity();
        vel.vx = 150;
        vel.vy = -75;
        vel.vz = 10;
        tr.velocity = vel;
        stress.Temperature temp = new stress.Temperature();
        temp.setValue(23.5);
        tr.temperature = temp;
        stress.PressureHpa pres = new stress.PressureHpa();
        pres.setValue(1013.25);
        tr.pressure = pres;
        stress.AngleDeg hdg = new stress.AngleDeg();
        hdg.setValue(270.0);
        tr.heading = hdg;
        stress.StatusFlags st = new stress.StatusFlags();
        st.setActive(true);
        st.setRecording(true);
        tr.status = st;
        tr.label = "SENSOR-ALPHA";
        return tr;
    }

    static stress.SurveillanceRecord makeSurveillanceTypical() {
        stress.SurveillanceRecord sr = new stress.SurveillanceRecord();
        sr.trackId = 1001;
        sr.timestamp = 1700002000;
        sr.reportClass = stress.ReportClass.PERIODIC;

        stress.SurveillanceRecordItems it = sr.items;
        it.sourceId = 42;
        stress.GeoPosition gp = new stress.GeoPosition();
        stress.Wgs84 lat = new stress.Wgs84();
        lat.setValue(41.015137);
        stress.Wgs84 lon = new stress.Wgs84();
        lon.setValue(28.979530);
        stress.AltitudeFl alt = new stress.AltitudeFl();
        alt.setValue(350.0);
        gp.latitude = lat;
        gp.longitude = lon;
        gp.altitude = alt;
        it.geoPosition = gp;
        stress.Coordinate cart = new stress.Coordinate();
        cart.x = 50000;
        cart.y = -30000;
        cart.z = 1400;
        it.cartesian = cart;
        stress.Velocity vel = new stress.Velocity();
        vel.vx = 200;
        vel.vy = -100;
        vel.vz = 5;
        it.velocity = vel;
        stress.AngleDeg hdg = new stress.AngleDeg();
        hdg.setValue(135.0);
        it.heading = hdg;
        stress.TrackQuality tq = new stress.TrackQuality();
        tq.confidence = 12;
        tq.freshness = 8;
        tq.source = 3;
        it.quality = tq;
        stress.ExtendedStatus es = new stress.ExtendedStatus();
        es.mode = 5;
        es.reliability = 10;
        es.secondaryMode = 2;
        es.auxFlags = 7;
        it.status = es;
        it.label = "TRK1001";
        stress.PeriodicPayload pp = new stress.PeriodicPayload();
        pp.intervalMs = 1000;
        pp.counter = 9999;
        sr.payload = pp;
        return sr;
    }

    static stress.SurveillanceRecord makeSurveillanceMaximal() {
        stress.SurveillanceRecord sr = new stress.SurveillanceRecord();
        sr.trackId = 2002;
        sr.timestamp = 1700003000;
        sr.reportClass = stress.ReportClass.EVENT_DRIVEN;

        stress.SurveillanceRecordItems it = sr.items;
        it.sourceId = 99;
        stress.GeoPosition gp = new stress.GeoPosition();
        stress.Wgs84 lat = new stress.Wgs84();
        lat.setValue(51.5074);
        stress.Wgs84 lon = new stress.Wgs84();
        lon.setValue(-0.1278);
        stress.AltitudeFl alt = new stress.AltitudeFl();
        alt.setValue(120.0);
        gp.latitude = lat;
        gp.longitude = lon;
        gp.altitude = alt;
        it.geoPosition = gp;
        stress.Coordinate cart = new stress.Coordinate();
        cart.x = -75000;
        cart.y = 120000;
        cart.z = -500;
        it.cartesian = cart;
        stress.Velocity vel = new stress.Velocity();
        vel.vx = -300;
        vel.vy = 250;
        vel.vz = -20;
        it.velocity = vel;
        stress.AngleDeg hdg = new stress.AngleDeg();
        hdg.setValue(45.0);
        it.heading = hdg;
        stress.AltitudeFl ar = new stress.AltitudeFl();
        ar.setValue(-12.5);
        it.altitudeRate = ar;
        stress.TrackQuality tq = new stress.TrackQuality();
        tq.confidence = 15;
        tq.freshness = 14;
        tq.source = 7;
        it.quality = tq;
        stress.ExtendedStatus es = new stress.ExtendedStatus();
        es.mode = 7;
        es.reliability = 15;
        es.secondaryMode = 5;
        es.auxFlags = 12;
        it.status = es;
        it.label = "MAXREC";
        stress.Temperature temp = new stress.Temperature();
        temp.setValue(-15.5);
        it.temperature = temp;
        stress.PressureHpa pres = new stress.PressureHpa();
        pres.setValue(250.0);
        it.pressure = pres;
        it.sensor = stress.SensorType.MAGNETIC;
        stress.ReadingsItem ri = new stress.ReadingsItem();
        ri.rep = 3;
        for (int ch = 0; ch < 3; ch++) {
            stress.ReadingsItemEntries e = new stress.ReadingsItemEntries();
            e.channel = ch;
            e.value = 1000 + ch * 100;
            e.quality = 90 + ch;
            ri.entries.add(e);
        }
        it.readings = ri;
        it.rawData = new byte[]{
            (byte)0xA0, (byte)0xA1, (byte)0xA2, (byte)0xA3,
            (byte)0xA4, (byte)0xA5, (byte)0xA6, (byte)0xA7,
            (byte)0xA8, (byte)0xA9, (byte)0xAA, (byte)0xAB,
            (byte)0xAC, (byte)0xAD, (byte)0xAE, (byte)0xAF
        };
        stress.EventPayload ep = new stress.EventPayload();
        ep.eventCode = 5001;
        ep.severity = stress.PriorityLevel.CRITICAL;
        ep.description = "Surveillance boundary alert";
        sr.payload = ep;
        return sr;
    }

    static byte[] encodeStress(Object msg) {
        stress.StressFrame frame = null;
        if (msg instanceof stress.Ping)
            frame = stress.StressFrame.wrap((stress.Ping) msg);
        else if (msg instanceof stress.TelemetryReport)
            frame = stress.StressFrame.wrap((stress.TelemetryReport) msg);
        else if (msg instanceof stress.SurveillanceRecord)
            frame = stress.StressFrame.wrap((stress.SurveillanceRecord) msg);
        else
            throw new IllegalArgumentException("Unknown stress message type");
        return frame.encodeBytes();
    }

    private BenchWireData() {}
}
