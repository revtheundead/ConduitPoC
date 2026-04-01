// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * Transport configuration for Conduit peers.
 * <p>
 * Use one of the concrete sub-classes for full per-transport options, or the
 * convenience factory methods for the common minimal case.
 *
 * <pre>{@code
 * // Minimal TCP client (uses all C++ defaults):
 * var cfg = TransportConfig.tcpClient("127.0.0.1:5000");
 *
 * // TCP client with custom reconnect policy:
 * var cfg = new TransportConfig.TcpClientConfig("127.0.0.1", 5000)
 *               .reconnect(new TransportConfig.ReconnectPolicy()
 *                   .initialDelayMs(2000)
 *                   .maxDelayMs(60000));
 *
 * // TCP server with explicit max-client limit:
 * var cfg = new TransportConfig.TcpServerConfig(5000).maxClients(32);
 *
 * // UDP with explicit bind and remote:
 * var cfg = new TransportConfig.UdpConfig()
 *               .bindAddress("0.0.0.0").bindPort(5000)
 *               .remoteAddress("192.168.1.10").remotePort(5001);
 *
 * // Serial port:
 * var cfg = new TransportConfig.SerialConfig("/dev/ttyUSB0", 115200)
 *               .dataBits(8)
 *               .parity(TransportConfig.SerialParity.NONE)
 *               .stopBits(TransportConfig.SerialStopBits.ONE);
 * }</pre>
 */
public class TransportConfig {

    // ====================================================================
    // Transport type enumeration
    // ====================================================================

    public enum TransportType {
        UDP(0), TCP_CLIENT(1), TCP_SERVER(2), SERIAL(3);

        private final int value;
        TransportType(int value) { this.value = value; }
        public int value() { return value; }
    }

    // ====================================================================
    // Serial configuration enums
    // ====================================================================

    /** Serial port parity (mirrors C++ {@code conduit::transceiver::transport::Parity}). */
    public enum SerialParity {
        NONE(0), ODD(1), EVEN(2);
        public final int value;
        SerialParity(int v) { this.value = v; }
    }

    /** Serial stop-bits (mirrors C++ {@code conduit::transceiver::transport::StopBits}). */
    public enum SerialStopBits {
        ONE(0), TWO(1);
        public final int value;
        SerialStopBits(int v) { this.value = v; }
    }

    /** Serial flow-control (mirrors C++ {@code conduit::transceiver::transport::FlowControl}). */
    public enum SerialFlowControl {
        NONE(0), HARDWARE(1), SOFTWARE(2);
        public final int value;
        SerialFlowControl(int v) { this.value = v; }
    }

    // ====================================================================
    // ReconnectPolicy (mirrors C++ ReconnectPolicy)
    // ====================================================================

    /**
     * Reconnect policy for TCP client (mirrors C++ {@code ReconnectPolicy}).
     * <p>
     * All fields default to the C++ library defaults; set only what you need.
     */
    public static final class ReconnectPolicy {
        /**
         * Whether reconnect is enabled.
         * {@code true} = use C++ default (enabled), {@code false} = disabled.
         */
        public boolean enabled           = true;
        /** Initial backoff delay in ms. 0 = use default (1000 ms). */
        public long    initialDelayMs    = 0;
        /** Maximum backoff delay in ms. 0 = use default (30000 ms). */
        public long    maxDelayMs        = 0;
        /** Backoff multiplier. 0.0 = use default (2.0). */
        public double  backoffMultiplier = 0.0;
        /** Maximum reconnect attempts. 0 = unlimited (C++ default). */
        public long    maxAttempts       = 0;

        public ReconnectPolicy enabled(boolean v)           { this.enabled = v; return this; }
        public ReconnectPolicy initialDelayMs(long v)       { this.initialDelayMs = v; return this; }
        public ReconnectPolicy maxDelayMs(long v)           { this.maxDelayMs = v; return this; }
        public ReconnectPolicy backoffMultiplier(double v)  { this.backoffMultiplier = v; return this; }
        public ReconnectPolicy maxAttempts(long v)          { this.maxAttempts = v; return this; }
    }

    // ====================================================================
    // Base fields (returned to the native layer)
    // ====================================================================

    private final TransportType type;
    // Primary address string ("host:port" or device path)
    protected String  address       = "";
    protected long    baudRate      = 0;
    // Extended fields — 0 = use C++ default
    protected long    recvBufferSize          = 0;
    // TCP client
    protected long    connectTimeoutMs        = 0;
    protected int     reconnectEnabled        = 0; // 0=default(on), >0=on, <0=off
    protected long    reconnectInitialDelayMs = 0;
    protected long    reconnectMaxDelayMs     = 0;
    protected double  reconnectBackoffMul     = 0.0;
    protected long    reconnectMaxAttempts    = 0;
    // UDP
    protected String  bindAddress    = null;
    protected int     bindPort       = 0;
    protected int     remotePort     = 0;
    protected long    maxDatagramSize = 0;
    protected long    maxPeers        = 0;
    protected long    peerTimeoutS    = 0;
    // UDP multicast
    protected String  multicastGroup     = null;
    protected String  multicastInterface = null;
    protected int     multicastTtl       = 0;
    protected int     multicastLoop      = 0;  // 0=default, 1=true, 2=false
    // TCP server
    protected long    maxClients = 0;
    // Serial
    protected int     dataBits    = 0;
    protected int     parity      = 0;
    protected int     stopBits    = 0;
    protected int     flowControl = 0;

    protected TransportConfig(TransportType type) {
        this.type = type;
    }

    public TransportType type()    { return type; }
    public String  address()       { return address; }
    public long    baudRate()      { return baudRate; }
    public long    recvBufferSize()         { return recvBufferSize; }
    public long    connectTimeoutMs()       { return connectTimeoutMs; }
    public int     reconnectEnabled()       { return reconnectEnabled; }
    public long    reconnectInitialDelayMs(){ return reconnectInitialDelayMs; }
    public long    reconnectMaxDelayMs()    { return reconnectMaxDelayMs; }
    public double  reconnectBackoffMul()    { return reconnectBackoffMul; }
    public long    reconnectMaxAttempts()   { return reconnectMaxAttempts; }
    public String  bindAddress()            { return bindAddress; }
    public int     bindPort()               { return bindPort; }
    public int     remotePort()             { return remotePort; }
    public long    maxDatagramSize()        { return maxDatagramSize; }
    public long    maxPeers()               { return maxPeers; }
    public long    peerTimeoutS()           { return peerTimeoutS; }
    public String  multicastGroup()        { return multicastGroup; }
    public String  multicastInterface()    { return multicastInterface; }
    public int     multicastTtl()          { return multicastTtl; }
    public int     multicastLoop()         { return multicastLoop; }
    public long    maxClients()            { return maxClients; }
    public int     dataBits()               { return dataBits; }
    public int     parity()                 { return parity; }
    public int     stopBits()               { return stopBits; }
    public int     flowControl()            { return flowControl; }

    // ====================================================================
    // Concrete per-transport config classes
    // ====================================================================

    /**
     * UDP transport configuration (mirrors C++ {@code UdpConfig}).
     * <p>
     * When only {@code address} is set the address is treated as the remote
     * endpoint and the socket binds to {@code 0.0.0.0:0}.  Use
     * {@link #bindAddress}/{@link #bindPort} to control the local socket.
     */
    public static final class UdpConfig extends TransportConfig {
        /** Create with explicit remote address string {@code "host:port"}. */
        public UdpConfig(String remoteAddress) {
            super(TransportType.UDP);
            this.address = remoteAddress;
        }
        /** Create with default (no remote address set). */
        public UdpConfig() { super(TransportType.UDP); }

        public UdpConfig remoteAddress(String addr)  { this.address = addr; return this; }
        public UdpConfig bindAddress(String addr)    { this.bindAddress = addr; return this; }
        public UdpConfig bindPort(int port)          { this.bindPort = port; return this; }
        public UdpConfig remotePort(int port)        { this.remotePort = port; return this; }
        public UdpConfig recvBufferSize(long sz)     { this.recvBufferSize = sz; return this; }
        public UdpConfig maxDatagramSize(long sz)    { this.maxDatagramSize = sz; return this; }
        public UdpConfig maxPeers(long n)            { this.maxPeers = n; return this; }
        public UdpConfig peerTimeoutSeconds(long s)  { this.peerTimeoutS = s; return this; }
        public UdpConfig multicastGroup(String group)  { this.multicastGroup = group; return this; }
        public UdpConfig multicastInterface(String ip) { this.multicastInterface = ip; return this; }
        public UdpConfig multicastTtl(int ttl)         { this.multicastTtl = ttl; return this; }
        public UdpConfig multicastLoop(boolean loop)   { this.multicastLoop = loop ? 1 : 2; return this; }
    }

    /**
     * TCP client transport configuration (mirrors C++ {@code TcpClientConfig}).
     */
    public static final class TcpClientConfig extends TransportConfig {
        /** Create with host and port. */
        public TcpClientConfig(String host, int port) {
            super(TransportType.TCP_CLIENT);
            this.address = host + ":" + port;
        }
        /** Create with combined {@code "host:port"} string. */
        public TcpClientConfig(String hostPort) {
            super(TransportType.TCP_CLIENT);
            this.address = hostPort;
        }

        public TcpClientConfig recvBufferSize(long sz)     { this.recvBufferSize = sz; return this; }
        public TcpClientConfig connectTimeoutMs(long ms)   { this.connectTimeoutMs = ms; return this; }

        /**
         * Set the reconnect policy.
         * Passing {@code null} or a policy with {@code enabled=false} disables reconnect.
         */
        public TcpClientConfig reconnect(ReconnectPolicy p) {
            if (p == null || !p.enabled) {
                this.reconnectEnabled = -1; // disabled
            } else {
                this.reconnectEnabled        = 1;
                this.reconnectInitialDelayMs = p.initialDelayMs;
                this.reconnectMaxDelayMs     = p.maxDelayMs;
                this.reconnectBackoffMul     = p.backoffMultiplier;
                this.reconnectMaxAttempts    = p.maxAttempts;
            }
            return this;
        }
    }

    /**
     * TCP server transport configuration (mirrors C++ {@code TcpServerConfig}).
     */
    public static final class TcpServerConfig extends TransportConfig {
        /** Create listening on all interfaces at the given port. */
        public TcpServerConfig(int port) {
            super(TransportType.TCP_SERVER);
            this.address = "0.0.0.0:" + port;
        }
        /** Create with combined {@code "[host]:port"} string. */
        public TcpServerConfig(String bindHostPort) {
            super(TransportType.TCP_SERVER);
            this.address = bindHostPort;
        }

        public TcpServerConfig recvBufferSize(long sz) { this.recvBufferSize = sz; return this; }
        public TcpServerConfig maxClients(long n)      { this.maxClients = n; return this; }
    }

    /**
     * Serial transport configuration (mirrors C++ {@code SerialConfig}).
     */
    public static final class SerialConfig extends TransportConfig {
        /** Create with port path and baud rate. */
        public SerialConfig(String port, long baudRate) {
            super(TransportType.SERIAL);
            this.address  = port;
            this.baudRate = baudRate;
        }

        public SerialConfig recvBufferSize(long sz)       { this.recvBufferSize = sz; return this; }
        public SerialConfig dataBits(int bits)            { this.dataBits = bits; return this; }
        public SerialConfig parity(SerialParity p)        { this.parity = p.value; return this; }
        public SerialConfig stopBits(SerialStopBits sb)   { this.stopBits = sb.value; return this; }
        public SerialConfig flowControl(SerialFlowControl fc) { this.flowControl = fc.value; return this; }
    }

    // ====================================================================
    // Convenience factory methods (simple "host:port" form)
    // ====================================================================

    /** Create a UDP transport config with a single {@code "host:port"} remote address. */
    public static TransportConfig udp(String remoteAddress) {
        return new UdpConfig(remoteAddress);
    }

    /** Create a TCP client transport config connecting to {@code "host:port"}. */
    public static TransportConfig tcpClient(String connectAddress) {
        return new TcpClientConfig(connectAddress);
    }

    /** Create a TCP server transport config listening on {@code ":port"} or {@code "host:port"}. */
    public static TransportConfig tcpServer(String listenAddress) {
        return new TcpServerConfig(listenAddress);
    }

    /** Create a serial transport config. */
    public static TransportConfig serial(String port, long baudRate) {
        return new SerialConfig(port, baudRate);
    }

    @Override
    public String toString() {
        return "TransportConfig{type=" + type + ", address='" + address + "'}";
    }
}
