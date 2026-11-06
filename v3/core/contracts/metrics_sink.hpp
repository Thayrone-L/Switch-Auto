#pragma once

#include <cstdint>

namespace switch_auto::contracts {

using MetricId = std::uint16_t;

// Metric identifiers are assigned by the owning module; this contract does not
// define protocol-specific IDs. Implementations must be thread-safe, bounded,
// non-blocking, allocation-free per call and must not format or write to disk.
// The sink is borrowed and must outlive every publisher.
class IMetricsSink {
public:
    virtual ~IMetricsSink() = default;

    virtual void increment(MetricId id, std::uint64_t amount = 1) noexcept = 0;
    virtual void observe(MetricId id, std::uint64_t value) noexcept = 0;
};

class NullMetricsSink final : public IMetricsSink {
public:
    void increment(MetricId, std::uint64_t) noexcept override {}
    void observe(MetricId, std::uint64_t) noexcept override {}
};

}  // namespace switch_auto::contracts
