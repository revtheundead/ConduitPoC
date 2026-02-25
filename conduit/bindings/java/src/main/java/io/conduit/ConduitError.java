// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * Exception thrown by Conduit bindings.
 */
public class ConduitError extends RuntimeException {
    private final int code;

    public ConduitError(int code, String message) {
        super("Conduit error " + code + ": " + message);
        this.code = code;
    }

    public int code() {
        return code;
    }
}
