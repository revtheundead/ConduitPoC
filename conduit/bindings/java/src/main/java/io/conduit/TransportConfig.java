// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * Transport configuration for Conduit peers.
 */
public final class TransportConfig {

    public enum TransportType {
        UDP(0), TCP_CLIENT(1), TCP_SERVER(2), SERIAL(3);

        private final int value;
        TransportType(int value) { this.value = value; }
        public int value() { return value; }
    }

    private final TransportType type;
    private final String address;
    private final int baudRate;

    public TransportConfig(TransportType type, String address, int baudRate) {
        this.type = type;
        this.address = address;
        this.baudRate = baudRate;
    }

    public TransportType type() { return type; }
    public String address() { return address; }
    public int baudRate() { return baudRate; }

    /** Create a UDP transport config. */
    public static TransportConfig udp(String bindAddress) {
        return new TransportConfig(TransportType.UDP, bindAddress, 0);
    }

    /** Create a TCP client transport config. */
    public static TransportConfig tcpClient(String connectAddress) {
        return new TransportConfig(TransportType.TCP_CLIENT, connectAddress, 0);
    }

    /** Create a TCP server transport config. */
    public static TransportConfig tcpServer(String listenAddress) {
        return new TransportConfig(TransportType.TCP_SERVER, listenAddress, 0);
    }

    @Override
    public String toString() {
        return "TransportConfig{type=" + type + ", address='" + address + "', baudRate=" + baudRate + "}";
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof TransportConfig)) return false;
        TransportConfig that = (TransportConfig) o;
        return baudRate == that.baudRate && type == that.type
            && (address != null ? address.equals(that.address) : that.address == null);
    }

    @Override
    public int hashCode() {
        int h = type.hashCode();
        h = 31 * h + (address != null ? address.hashCode() : 0);
        h = 31 * h + baudRate;
        return h;
    }
}
