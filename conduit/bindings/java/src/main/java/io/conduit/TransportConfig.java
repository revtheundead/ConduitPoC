// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * Transport configuration for Conduit peers.
 */
public record TransportConfig(TransportType type, String address, int baudRate) {

    public enum TransportType {
        UDP(0), TCP_CLIENT(1), TCP_SERVER(2), SERIAL(3);

        private final int value;
        TransportType(int value) { this.value = value; }
        public int value() { return value; }
    }

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
}
