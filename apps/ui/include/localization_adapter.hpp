#pragma once

#include "switch_auto/localization.hpp"

namespace switch_auto::ui {

class BorealisLocalizationService final : public LocalizationService {
public:
    std::string_view locale() const override { return locale_; }
    bool setLocale(std::string_view locale) override;
    std::string translate(std::string_view key) const override;

private:
    std::string locale_ = "auto";
};

void setLocalizationService(LocalizationService* service);
std::string tr(std::string_view key);

}  // namespace switch_auto::ui
