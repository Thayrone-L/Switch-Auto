#include "switch_auto/localization.hpp"

#include <algorithm>

namespace switch_auto {

bool CatalogLocalizationService::addCatalog(
    std::string locale, TranslationCatalog catalog) {
    if (locale.empty() || catalog.empty()) return false;
    const bool invalid = std::any_of(catalog.begin(), catalog.end(),
        [](const auto& entry) {
            return entry.first.empty() || entry.second.empty();
        });
    if (invalid) return false;
    return catalogs_.emplace(std::move(locale), std::move(catalog)).second;
}

bool CatalogLocalizationService::setLocale(std::string_view locale) {
    if (catalogs_.find(std::string(locale)) == catalogs_.end()) return false;
    locale_ = locale;
    return true;
}

bool CatalogLocalizationService::hasTranslation(
    std::string_view locale, std::string_view key) const {
    const auto catalog = catalogs_.find(std::string(locale));
    return catalog != catalogs_.end() &&
        catalog->second.find(std::string(key)) != catalog->second.end();
}

std::string CatalogLocalizationService::translate(std::string_view key) const {
    const auto find = [key, this](std::string_view locale) -> const std::string* {
        const auto catalog = catalogs_.find(std::string(locale));
        if (catalog == catalogs_.end()) return nullptr;
        const auto translation = catalog->second.find(std::string(key));
        return translation == catalog->second.end() ? nullptr : &translation->second;
    };
    if (const std::string* translated = find(locale_)) return *translated;
    if (locale_ != fallbackLocale()) {
        if (const std::string* fallback = find(fallbackLocale())) return *fallback;
    }
    return std::string(key);
}

}  // namespace switch_auto
