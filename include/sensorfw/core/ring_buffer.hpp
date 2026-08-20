/**
 * @file ring_buffer.hpp
 * @brief Fixed-capacity circular buffer used by the window based filters.
 */
#ifndef SENSORFW_CORE_RING_BUFFER_HPP
#define SENSORFW_CORE_RING_BUFFER_HPP

#include "sensorfw/config.hpp"

namespace sensorfw {

template <typename T, uint8_t Capacity>
class RingBuffer {
public:
    RingBuffer() : head_(0), size_(0) {}

    /** @brief Append, overwriting the oldest element when full. */
    void push(const T& value) {
        buffer_[head_] = value;
        head_ = static_cast<uint8_t>((head_ + 1u) % Capacity);
        if (size_ < Capacity) { ++size_; }
    }

    /** @brief Element @p index counted from the oldest (0 = oldest). */
    const T& at(uint8_t index) const {
        const uint8_t start = static_cast<uint8_t>((head_ + Capacity - size_) % Capacity);
        return buffer_[(start + index) % Capacity];
    }

    /** @brief Most recently pushed element. Undefined when empty. */
    const T& newest() const { return buffer_[(head_ + Capacity - 1u) % Capacity]; }

    void    clear()   { head_ = 0; size_ = 0; }
    uint8_t size()  const { return size_; }
    bool    full()  const { return size_ == Capacity; }
    bool    empty() const { return size_ == 0; }
    static uint8_t capacity() { return Capacity; }

private:
    T       buffer_[Capacity];
    uint8_t head_;
    uint8_t size_;
};

} // namespace sensorfw

#endif // SENSORFW_CORE_RING_BUFFER_HPP
