#pragma once

#include "core/base/errors.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace switch_auto::test {

struct CallFault {
    std::size_t call = 0;
    base::TransportError error{};
};

// Deterministic operation-level faults. Call numbers are one-based. A fault is
// consumed only by the matching call, allowing a test to model recovery.
class IoFaultPlan {
public:
    void setMaxChunk(std::size_t bytes) noexcept {
        max_chunk_ = bytes == 0 ? 1 : bytes;
    }

    [[nodiscard]] std::size_t limit(std::size_t requested) const noexcept {
        return requested < max_chunk_ ? requested : max_chunk_;
    }

    void failCall(std::size_t call, base::TransportError error) {
        faults_.push_back({call, error});
    }

    [[nodiscard]] std::optional<base::TransportError> faultFor(
        std::size_t call) const noexcept {
        for (const auto& fault : faults_) {
            if (fault.call == call) {
                return fault.error;
            }
        }
        return std::nullopt;
    }

private:
    std::size_t max_chunk_ = std::numeric_limits<std::size_t>::max();
    std::vector<CallFault> faults_;
};

}  // namespace switch_auto::test
