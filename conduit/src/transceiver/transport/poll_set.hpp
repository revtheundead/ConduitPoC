// SPDX-License-Identifier: MIT
// Conduit - PollSet: poll()-based I/O multiplexer (POSIX only)
//
// Replaces select() on POSIX platforms to avoid the FD_SETSIZE limit.
// On Windows, select() uses an FD array (not a bitmask) so this is not needed.

#pragma once
#ifndef _WIN32

#include <poll.h>
#include <vector>

namespace conduit::transceiver::transport {

class PollSet {
public:
    /// Register interest in readability for the given fd.
    void add_read(int fd);

    /// Register interest in writability for the given fd.
    void add_write(int fd);

    /// Block until at least one fd is ready or timeout expires.
    /// @param timeout_ms  Timeout in milliseconds (-1 for infinite).
    /// @return  Number of ready fds, 0 on timeout, -1 on error.
    int wait(int timeout_ms);

    /// Check whether fd was reported readable after wait().
    bool is_readable(int fd) const;

    /// Check whether fd was reported writable after wait().
    bool is_writable(int fd) const;

    /// Check whether fd has an error/hangup/invalid condition after wait().
    bool has_error(int fd) const;

private:
    std::vector<struct pollfd> fds_;

    /// Linear search for fd; returns index or -1.
    int find_fd(int fd) const;
};

} // namespace conduit::transceiver::transport

#endif // !_WIN32
