#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace switch_auto::base {

template <typename Byte>
class BasicByteSpan {
public:
    using element_type = Byte;
    using pointer = Byte*;

    constexpr BasicByteSpan() noexcept = default;
    // Precondition: data is non-null whenever size is non-zero. The span does
    // not own the storage, which must remain alive for the span's lifetime.
    constexpr BasicByteSpan(pointer data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    template <std::size_t Size>
    constexpr BasicByteSpan(Byte (&data)[Size]) noexcept
        : data_(data), size_(Size) {}

    template <typename OtherByte,
              typename = std::enable_if_t<std::is_convertible_v<OtherByte (*)[],
                                                               Byte (*)[]>>>
    constexpr BasicByteSpan(BasicByteSpan<OtherByte> other) noexcept
        : data_(other.data()), size_(other.size()) {}

    [[nodiscard]] constexpr pointer data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    // Precondition: index < size(). Bounds are deliberately not checked on the
    // hot path; callers that need slicing should use subspan(), which clamps.
    [[nodiscard]] constexpr Byte& operator[](std::size_t index) const noexcept {
        return data_[index];
    }

    // Returns an empty span when offset is invalid and clamps count to the
    // available storage. The returned span remains non-owning.
    [[nodiscard]] constexpr BasicByteSpan subspan(
        std::size_t offset, std::size_t count) const noexcept {
        if (offset > size_) {
            return {};
        }
        const auto remaining = size_ - offset;
        const auto bounded_count = count < remaining ? count : remaining;
        return {offset == 0 ? data_ : data_ + offset, bounded_count};
    }

    [[nodiscard]] constexpr BasicByteSpan subspan(
        std::size_t offset) const noexcept {
        if (offset > size_) {
            return {};
        }
        return {offset == 0 ? data_ : data_ + offset, size_ - offset};
    }

private:
    pointer data_ = nullptr;
    std::size_t size_ = 0;
};

using ByteSpan = BasicByteSpan<std::uint8_t>;
using ConstByteSpan = BasicByteSpan<const std::uint8_t>;

}  // namespace switch_auto::base
