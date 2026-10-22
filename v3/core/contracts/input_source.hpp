#pragma once

#include "core/base/cancellation.hpp"
#include "core/base/result.hpp"

#include <cstdint>

namespace switch_auto::contracts {

enum class InputAction : std::uint8_t {
    down,
    move,
    up,
    cancel,
};

struct InputEvent {
    InputAction action = InputAction::cancel;
    std::uint32_t pointer_id = 0;
    float x = 0.0F;
    float y = 0.0F;
    std::uint64_t timestamp_us = 0;
};

enum class InputReadStatus : std::uint8_t {
    event,
    timed_out,
    woken_up,
};

struct InputReadResult {
    InputReadStatus status = InputReadStatus::timed_out;
    InputEvent event{};
};

enum class InputErrorCode : std::uint8_t {
    unavailable,
    cancelled,
    device_failure,
};

struct InputError {
    InputErrorCode code = InputErrorCode::device_failure;
    std::int32_t implementation_code = 0;
};

// One input worker owns read(). Events use source/display coordinates; protocol
// conversion happens elsewhere. A successful `event` result contains exactly
// one complete event. Deadline expiry is a successful timed_out result.
// wakeup() may be called from another thread and returns woken_up unless the
// token was cancelled, in which case read() fails with cancelled. No event may
// be retained by the source after read() returns.
class IInputSource {
public:
    virtual ~IInputSource() = default;

    [[nodiscard]] virtual base::Result<InputReadResult, InputError> read(
        base::Deadline deadline,
        base::CancellationToken cancellation) noexcept = 0;

    virtual void wakeup() noexcept = 0;
};

}  // namespace switch_auto::contracts
