#include "switch_auto/widget_layout.hpp"

#include <algorithm>
#include <charconv>

namespace switch_auto {
namespace {

bool intersects(const WidgetPlacement& left, const WidgetPlacement& right) {
    if (!left.visible || !right.visible) return false;
    return left.column < right.column + right.columnSpan &&
           left.column + left.columnSpan > right.column &&
           left.row < right.row + right.rowSpan &&
           left.row + left.rowSpan > right.row;
}

std::optional<int> integerField(std::string_view object, std::string_view name) {
    const std::string key = "\"" + std::string(name) + "\":";
    const auto position = object.find(key);
    if (position == std::string_view::npos) return std::nullopt;
    const char* begin = object.data() + position + key.size();
    int value = 0;
    const auto parsed = std::from_chars(begin, object.data() + object.size(), value);
    return parsed.ec == std::errc{} ? std::optional<int>(value) : std::nullopt;
}

std::optional<std::string> stringField(
    std::string_view object, std::string_view name) {
    const std::string key = "\"" + std::string(name) + "\":\"";
    const auto position = object.find(key);
    if (position == std::string_view::npos) return std::nullopt;
    const auto start = position + key.size();
    const auto end = object.find('"', start);
    return end == std::string_view::npos
        ? std::nullopt
        : std::optional<std::string>(object.substr(start, end - start));
}

}  // namespace

WidgetLayout WidgetLayout::defaultAutomotive() {
    WidgetLayout layout;
    layout.add({"core.gps", 0, 0, 8, 8, 5, 4, true});
    layout.add({"core.speed", 8, 0, 4, 2, 2, 2, true});
    layout.add({"core.media", 8, 2, 4, 6, 3, 3, true});
    return layout;
}

bool WidgetLayout::add(WidgetPlacement placement) {
    if (find(placement.widgetId) ||
        validatePlacement(placement) != WidgetLayoutError::None) return false;
    placements_.push_back(std::move(placement));
    return true;
}

WidgetLayoutError WidgetLayout::move(
    std::string_view widgetId, int column, int row) {
    auto entry = std::find_if(placements_.begin(), placements_.end(),
        [widgetId](const auto& value) { return value.widgetId == widgetId; });
    if (entry == placements_.end()) return WidgetLayoutError::UnknownWidget;
    WidgetPlacement candidate = *entry;
    candidate.column = column;
    candidate.row = row;
    const auto error = validatePlacement(candidate, widgetId);
    if (error == WidgetLayoutError::None) *entry = std::move(candidate);
    return error;
}

WidgetLayoutError WidgetLayout::resize(
    std::string_view widgetId, int columns, int rows) {
    auto entry = std::find_if(placements_.begin(), placements_.end(),
        [widgetId](const auto& value) { return value.widgetId == widgetId; });
    if (entry == placements_.end()) return WidgetLayoutError::UnknownWidget;
    WidgetPlacement candidate = *entry;
    candidate.columnSpan = columns;
    candidate.rowSpan = rows;
    const auto error = validatePlacement(candidate, widgetId);
    if (error == WidgetLayoutError::None) *entry = std::move(candidate);
    return error;
}

WidgetLayoutError WidgetLayout::setVisible(
    std::string_view widgetId, bool visible) {
    auto entry = std::find_if(placements_.begin(), placements_.end(),
        [widgetId](const auto& value) { return value.widgetId == widgetId; });
    if (entry == placements_.end()) return WidgetLayoutError::UnknownWidget;
    WidgetPlacement candidate = *entry;
    candidate.visible = visible;
    const auto error = validatePlacement(candidate, widgetId);
    if (error == WidgetLayoutError::None) *entry = std::move(candidate);
    return error;
}

WidgetLayoutError WidgetLayout::validate() const {
    for (const auto& placement : placements_) {
        const auto error = validatePlacement(placement, placement.widgetId);
        if (error != WidgetLayoutError::None) return error;
    }
    return WidgetLayoutError::None;
}

WidgetLayoutError WidgetLayout::validatePlacement(
    const WidgetPlacement& candidate, std::string_view ignoredId) const {
    if (candidate.widgetId.empty()) return WidgetLayoutError::UnknownWidget;
    if (candidate.columnSpan < candidate.minimumColumnSpan ||
        candidate.rowSpan < candidate.minimumRowSpan) {
        return WidgetLayoutError::BelowMinimumSize;
    }
    if (candidate.column < 0 || candidate.row < 0 ||
        candidate.columnSpan <= 0 || candidate.rowSpan <= 0 ||
        candidate.column + candidate.columnSpan > kWidgetGridColumns ||
        candidate.row + candidate.rowSpan > kWidgetGridRows) {
        return WidgetLayoutError::OutOfBounds;
    }
    for (const auto& placement : placements_) {
        if (placement.widgetId == ignoredId) continue;
        if (placement.widgetId == candidate.widgetId) {
            return WidgetLayoutError::DuplicateWidget;
        }
        if (intersects(candidate, placement)) return WidgetLayoutError::Overlap;
    }
    return WidgetLayoutError::None;
}

const WidgetPlacement* WidgetLayout::find(std::string_view widgetId) const {
    const auto entry = std::find_if(placements_.begin(), placements_.end(),
        [widgetId](const auto& value) { return value.widgetId == widgetId; });
    return entry == placements_.end() ? nullptr : &*entry;
}

std::string WidgetLayout::serialize() const {
    std::string output = "{\"schemaVersion\":" +
        std::to_string(kWidgetLayoutSchemaVersion) + ",\"widgets\":[";
    for (std::size_t index = 0; index < placements_.size(); ++index) {
        const auto& item = placements_[index];
        if (index) output += ',';
        output += "{\"id\":\"" + item.widgetId + "\",\"column\":" +
            std::to_string(item.column) + ",\"row\":" + std::to_string(item.row) +
            ",\"columnSpan\":" + std::to_string(item.columnSpan) +
            ",\"rowSpan\":" + std::to_string(item.rowSpan) +
            ",\"minColumns\":" + std::to_string(item.minimumColumnSpan) +
            ",\"minRows\":" + std::to_string(item.minimumRowSpan) +
            ",\"visible\":" + std::string(item.visible ? "1" : "0") + "}";
    }
    return output + "]}";
}

std::optional<WidgetLayout> WidgetLayout::deserialize(std::string_view payload) {
    const auto schema = integerField(payload, "schemaVersion");
    if (!schema || *schema != static_cast<int>(kWidgetLayoutSchemaVersion)) {
        return std::nullopt;
    }
    WidgetLayout result;
    auto position = payload.find("\"widgets\":[");
    if (position == std::string_view::npos) return std::nullopt;
    position = payload.find('{', position);
    while (position != std::string_view::npos) {
        const auto end = payload.find('}', position);
        if (end == std::string_view::npos) return std::nullopt;
        const auto object = payload.substr(position, end - position + 1);
        const auto id = stringField(object, "id");
        const auto column = integerField(object, "column");
        const auto row = integerField(object, "row");
        const auto columns = integerField(object, "columnSpan");
        const auto rows = integerField(object, "rowSpan");
        const auto minColumns = integerField(object, "minColumns");
        const auto minRows = integerField(object, "minRows");
        const auto visible = integerField(object, "visible");
        if (!id || !column || !row || !columns || !rows || !minColumns ||
            !minRows || !visible ||
            !result.add({*id, *column, *row, *columns, *rows,
                *minColumns, *minRows, *visible != 0})) return std::nullopt;
        position = payload.find('{', end + 1);
    }
    return result.validate() == WidgetLayoutError::None
        ? std::optional<WidgetLayout>(std::move(result)) : std::nullopt;
}

const char* toString(WidgetLayoutError error) {
    switch (error) {
        case WidgetLayoutError::None: return "none";
        case WidgetLayoutError::UnknownWidget: return "unknown_widget";
        case WidgetLayoutError::DuplicateWidget: return "duplicate_widget";
        case WidgetLayoutError::OutOfBounds: return "out_of_bounds";
        case WidgetLayoutError::BelowMinimumSize: return "below_minimum_size";
        case WidgetLayoutError::Overlap: return "overlap";
    }
    return "unknown";
}

}  // namespace switch_auto
