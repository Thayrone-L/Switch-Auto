#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace switch_auto {

using TranslationCatalog = std::unordered_map<std::string, std::string>;

class LocalizationService {
public:
    virtual ~LocalizationService() = default;
    virtual std::string_view locale() const = 0;
    virtual bool setLocale(std::string_view locale) = 0;
    virtual std::string translate(std::string_view key) const = 0;
};

class CatalogLocalizationService final : public LocalizationService {
public:
    static constexpr std::string_view fallbackLocale() { return "en-US"; }

    bool addCatalog(std::string locale, TranslationCatalog catalog);
    std::string_view locale() const override { return locale_; }
    bool setLocale(std::string_view locale) override;
    std::string translate(std::string_view key) const override;
    bool hasTranslation(std::string_view locale, std::string_view key) const;

private:
    std::unordered_map<std::string, TranslationCatalog> catalogs_;
    std::string locale_ = std::string(fallbackLocale());
};

}  // namespace switch_auto
