#ifndef BUFFER_CONTROL_H
#define BUFFER_CONTROL_H

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>

// All buffer users take a short work lease. Clearing closes admission, waits
// for existing leases, then resets hardware and memory with exclusive access.
struct BufferControl {
    std::mutex mutex;
    std::mutex clear_mutex;
    std::condition_variable idle;
    std::atomic<bool> clearing{false};
    std::atomic<unsigned long> epoch{0};
    unsigned active = 0;
};
inline BufferControl& buffer_control() { static BufferControl state; return state; }
inline bool buffer_clear_requested() { return buffer_control().clearing.load(); }
inline unsigned long buffer_epoch() { return buffer_control().epoch.load(); }
class BufferWork {
    bool entered_;
public:
    BufferWork() {
        auto& s = buffer_control();
        std::lock_guard<std::mutex> lock(s.mutex);
        entered_ = !s.clearing.load();
        if(entered_) ++s.active;
    }
    ~BufferWork() {
        if(!entered_) return;
        auto& s = buffer_control();
        std::lock_guard<std::mutex> lock(s.mutex);
        if(--s.active == 0) s.idle.notify_all();
    }
    explicit operator bool() const { return entered_; }
    BufferWork(const BufferWork&) = delete;
    BufferWork& operator=(const BufferWork&) = delete;
};
#endif
