#include "localization_adapter.hpp"

#include <borealis.hpp>

namespace switch_auto::ui {
namespace {
LocalizationService* currentService = nullptr;
}

bool BorealisLocalizationService::setLocale(std::string_view locale) {
    if (locale.empty()) return false;
    locale_ = locale;
    return true;
}

std::string BorealisLocalizationService::translate(std::string_view key) const {
    const std::string translated = brls::getStr(std::string(key));
    return translated.empty() ? std::string(key) : translated;
}

void setLocalizationService(LocalizationService* service) {
    currentService = service;
}

std::string tr(std::string_view key) {
    return currentService ? currentService->translate(key) : std::string(key);
}

}  // namespace switch_auto::ui
