#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

/**
 * Thin RAII wrapper around Linux epoll API.
 *
 * Usage:
 *   EpollPoller poller;
 *   poller.add(fd, EPOLLIN);
 *   int n = poller.wait(events, timeout_ms);
 *   poller.remove(fd);
 */
class EpollPoller {
public:
    EpollPoller() {
        fd_ = epoll_create1(0);
        if (fd_ < 0)
            throw std::runtime_error(std::string("epoll_create1: ") + strerror(errno));
    }

    ~EpollPoller() {
        if (fd_ >= 0) ::close(fd_);
    }

    EpollPoller(const EpollPoller &) = delete;
    EpollPoller &operator=(const EpollPoller &) = delete;

    void add(int fd, uint32_t events, uint64_t data = 0) {
        struct epoll_event ev;
        ev.events  = events;
        ev.data.u64 = data;
        if (epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
            throw std::runtime_error(std::string("epoll_ctl ADD: ") + strerror(errno));
    }

    void modify(int fd, uint32_t events, uint64_t data = 0) {
        struct epoll_event ev;
        ev.events  = events;
        ev.data.u64 = data;
        if (epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
            throw std::runtime_error(std::string("epoll_ctl MOD: ") + strerror(errno));
    }

    void remove(int fd) {
        epoll_ctl(fd_, EPOLL_CTL_DEL, fd, nullptr);
    }

    /* Wait for events. Returns number of ready fds (0 = timeout).
     * Returns -1 on error (except EINTR which is silently retried). */
    int wait(struct epoll_event *events, int max_events, int timeout_ms) {
        int n;
        do { n = epoll_wait(fd_, events, max_events, timeout_ms); }
        while (n < 0 && errno == EINTR);
        return n;
    }

    int fd() const { return fd_; }

private:
    int fd_ = -1;
};
