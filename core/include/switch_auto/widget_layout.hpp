#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace switch_auto {

inline constexpr std::uint32_t kWidgetLayoutSchemaVersion = 1;
inline constexpr int kWidgetGridColumns = 12;
inline constexpr int kWidgetGridRows = 8;

struct WidgetPlacement {
    std::string widgetId;
    int column = 0;
    int row = 0;
    int columnSpan = 1;
    int rowSpan = 1;
    int minimumColumnSpan = 1;
    int minimumRowSpan = 1;
    bool visible = true;
};

enum class WidgetLayoutError {
    None,
    UnknownWidget,
    DuplicateWidget,
    OutOfBounds,
    BelowMinimumSize,
    Overlap,
};

class WidgetLayout {
public:
    static WidgetLayout defaultAutomotive();

    bool add(WidgetPlacement placement);
    WidgetLayoutError move(std::string_view widgetId, int column, int row);
    WidgetLayoutError resize(std::string_view widgetId, int columns, int rows);
    WidgetLayoutError setVisible(std::string_view widgetId, bool visible);
    WidgetLayoutError validate() const;

    const std::vector<WidgetPlacement>& placements() const { return placements_; }
    const WidgetPlacement* find(std::string_view widgetId) const;
    std::string serialize() const;
    static std::optional<WidgetLayout> deserialize(std::string_view payload);

private:
    WidgetLayoutError validatePlacement(
        const WidgetPlacement& candidate, std::string_view ignoredId = {}) const;
    std::vector<WidgetPlacement> placements_;
};

const char* toString(WidgetLayoutError error);

}  // namespace switch_auto
