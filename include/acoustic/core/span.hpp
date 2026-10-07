#pragma once

#include <cstddef>
#include <type_traits>

namespace acoustic {

template <typename T>
class Span {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;

    constexpr Span() noexcept = default;
    constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}

    template <std::size_t N>
    constexpr Span(T (&array)[N]) noexcept : data_(array), size_(N) {}

    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr T& operator[](std::size_t index) const noexcept { return data_[index]; }
    [[nodiscard]] constexpr T* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }

private:
    T* data_{nullptr};
    std::size_t size_{0};
};

template <typename T>
class StridedSpan {
public:
    constexpr StridedSpan() noexcept = default;
    constexpr StridedSpan(T* data, std::size_t size, std::ptrdiff_t stride) noexcept
        : data_(data), size_(size), stride_(stride) {}

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr std::ptrdiff_t stride() const noexcept { return stride_; }

    constexpr T& operator[](std::size_t index) const noexcept {
        return *(data_ + static_cast<std::ptrdiff_t>(index) * stride_);
    }

private:
    T* data_{nullptr};
    std::size_t size_{0};
    std::ptrdiff_t stride_{1};
};

}  // namespace acoustic
