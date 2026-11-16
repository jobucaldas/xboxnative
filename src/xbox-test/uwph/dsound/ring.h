#pragma once
#include <cstdint>

namespace dsound_core {
struct Regions {
    uint32_t first_bytes = 0;
    uint32_t second_bytes = 0;
    uint32_t second_offset = 0;
};

class Ring {
public:
    Ring(uint8_t* data, uint32_t bytes, uint32_t align)
        : data_(data), bytes_(bytes), align_(align) {}

    bool lock(uint32_t offset, uint32_t bytes, bool entire, Regions& out) {
        if (!data_ || !bytes_ || !align_ || locked_) return false;
        if (entire) { offset = 0; bytes = bytes_; }
        if (offset >= bytes_ || !bytes || bytes > bytes_ || offset % align_ || bytes % align_) return false;
        out.first_bytes = bytes < bytes_ - offset ? bytes : bytes_ - offset;
        out.second_bytes = bytes - out.first_bytes;
        out.second_offset = 0;
        lock_offset_ = offset;
        lock_bytes_ = bytes;
        locked_ = true;
        return true;
    }

    bool unlock(uint32_t offset, uint32_t bytes) {
        if (!locked_ || offset != lock_offset_ || bytes != lock_bytes_) return false;
        locked_ = false;
        lock_bytes_ = 0;
        return true;
    }

    uint32_t cursor(uint64_t samples_played, uint32_t start, bool looping) const {
        if (!bytes_ || !align_) return 0;
        uint64_t consumed = samples_played * align_;
        uint64_t position = uint64_t(start) + consumed;
        if (looping) position %= bytes_;
        else if (position > bytes_) position = bytes_;
        return uint32_t(position - position % align_);
    }

    bool crossed(uint64_t previous_samples, uint64_t current_samples, uint32_t start,
                 uint32_t target, bool looping) const {
        if (!bytes_ || !align_ || target >= bytes_ || current_samples <= previous_samples) return false;
        const uint64_t previous = uint64_t(start) + previous_samples * align_;
        const uint64_t current = uint64_t(start) + current_samples * align_;
        if (!looping) return previous < target && target <= current;
        uint64_t next = (previous / bytes_) * bytes_ + target;
        if (next <= previous) next += bytes_;
        return next <= current;
    }

    uint32_t bytes() const { return bytes_; }
    uint32_t align() const { return align_; }
    uint8_t* data(uint32_t offset = 0) const { return data_ + offset; }
    bool locked() const { return locked_; }
    uint32_t lock_offset() const { return lock_offset_; }
    uint32_t lock_bytes() const { return lock_bytes_; }

private:
    uint8_t* data_ = nullptr;
    uint32_t bytes_ = 0;
    uint32_t align_ = 0;
    uint32_t lock_offset_ = 0;
    uint32_t lock_bytes_ = 0;
    bool locked_ = false;
};
}
