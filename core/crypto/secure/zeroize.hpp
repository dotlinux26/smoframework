#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smo {
namespace secure {

void zeroize(volatile void* ptr, size_t len) noexcept;

template <typename T>
void zeroize(T& obj) noexcept {
    zeroize(&obj, sizeof(obj));
}

class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(size_t n) : data_(n, 0) {}
    SecureBuffer(const uint8_t* p, size_t n) : data_(p, p + n) {}
    ~SecureBuffer() noexcept { cleanse(); }

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& other) noexcept
        : data_(std::move(other.data_)) {}
    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
        if (this != &other) {
            cleanse();
            data_ = std::move(other.data_);
        }
        return *this;
    }

    uint8_t* data() noexcept { return data_.data(); }
    const uint8_t* data() const noexcept { return data_.data(); }
    size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }

    void resize(size_t n) {
        if (n < data_.size()) {
            zeroize(data_.data() + n, data_.size() - n);
        }
        data_.resize(n);
    }

    void cleanse() noexcept {
        if (!data_.empty()) {
            zeroize(data_.data(), data_.size());
            data_.clear();
        }
    }

private:
    std::vector<uint8_t> data_;
};

} // namespace secure
} // namespace smo
