#pragma once

#include <functional>
#include <stdexcept>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace networking {

struct EventData {
  int fd;
  bool is_timer;
};

class EventLoop {
public:
  EventLoop() {
    kq_ = kqueue();
    if (kq_ == -1) {
      throw std::runtime_error("Failed to create kqueue");
    }
  }

  ~EventLoop() {
    if (kq_ != -1) {
      close(kq_);
    }
  }

  // Register a socket for read events
  void register_read(int fd, EventData *user_data) {
    struct kevent evSet;
    EV_SET(&evSet, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, user_data);
    if (kevent(kq_, &evSet, 1, nullptr, 0, nullptr) == -1) {
      throw std::runtime_error("Failed to register read event");
    }
  }

  // Register a periodic timer (in milliseconds)
  void register_timer(int timer_id, int interval_ms, EventData *user_data) {
    struct kevent evSet;
    EV_SET(&evSet, timer_id, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, interval_ms,
           user_data);
    if (kevent(kq_, &evSet, 1, nullptr, 0, nullptr) == -1) {
      throw std::runtime_error("Failed to register timer event");
    }
  }

  void poll(std::function<void(EventData *, bool)> cb) {
    struct kevent evList[32];

    int num_events = kevent(kq_, nullptr, 0, evList, 32, nullptr);
    if (num_events == -1) {
      throw std::runtime_error("kevent polling failed");
    }

    for (int i = 0; i < num_events; i++) {
      EventData *data = static_cast<EventData *>(evList[i].udata);
      bool is_eof = (evList[i].flags & EV_EOF);
      cb(data, is_eof);
    }
  }

private:
  int kq_;
};

} // namespace networking
