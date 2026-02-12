// SPDX-License-Identifier: MIT
// Conduit - PollSet implementation (POSIX only)

#ifndef _WIN32

#include "poll_set.hpp"

#include <algorithm>

namespace conduit::transceiver::transport {

void PollSet::add_read(int fd) {
    int idx = find_fd(fd);
    if (idx >= 0) {
        fds_[static_cast<size_t>(idx)].events |= POLLIN;
    } else {
        fds_.push_back({fd, POLLIN, 0});
    }
}

void PollSet::add_write(int fd) {
    int idx = find_fd(fd);
    if (idx >= 0) {
        fds_[static_cast<size_t>(idx)].events |= POLLOUT;
    } else {
        fds_.push_back({fd, POLLOUT, 0});
    }
}

int PollSet::wait(int timeout_ms) {
    return ::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeout_ms);
}

bool PollSet::is_readable(int fd) const {
    int idx = find_fd(fd);
    if (idx < 0) return false;
    return (fds_[static_cast<size_t>(idx)].revents & POLLIN) != 0;
}

bool PollSet::is_writable(int fd) const {
    int idx = find_fd(fd);
    if (idx < 0) return false;
    return (fds_[static_cast<size_t>(idx)].revents & POLLOUT) != 0;
}

bool PollSet::has_error(int fd) const {
    int idx = find_fd(fd);
    if (idx < 0) return false;
    return (fds_[static_cast<size_t>(idx)].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
}

int PollSet::find_fd(int fd) const {
    for (size_t i = 0; i < fds_.size(); ++i) {
        if (fds_[i].fd == fd) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace conduit::transceiver::transport

#endif // !_WIN32
