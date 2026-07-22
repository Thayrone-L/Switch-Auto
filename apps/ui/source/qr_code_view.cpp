#include "qr_code_view.hpp"

#include "qrcodegen.hpp"

#include <algorithm>
#include <cmath>

namespace switch_auto::ui {

QrCodeView::QrCodeView() = default;
QrCodeView::~QrCodeView() = default;

void QrCodeView::setPayload(const std::string& payload) {
    if (payload.empty()) {
        code_.reset();
        return;
    }
    code_ = std::make_unique<qrcodegen::QrCode>(
        qrcodegen::QrCode::encodeText(payload.c_str(), qrcodegen::QrCode::Ecc::MEDIUM));
}

void QrCodeView::draw(NVGcontext* vg, float x, float y, float width, float height,
    brls::Style, brls::FrameContext*) {
    if (!code_) return;
    constexpr int quietZone = 4;
    const int modules = code_->getSize();
    const float cell = std::floor(std::min(width, height) /
        static_cast<float>(modules + quietZone * 2));
    const float size = cell * static_cast<float>(modules + quietZone * 2);
    const float left = x + (width - size) * 0.5f;
    const float top = y + (height - size) * 0.5f;

    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgRect(vg, left, top, size, size);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    for (int row = 0; row < modules; ++row) {
        for (int column = 0; column < modules; ++column) {
            if (code_->getModule(column, row)) {
                nvgRect(vg, left + (column + quietZone) * cell,
                    top + (row + quietZone) * cell, cell, cell);
            }
        }
    }
    nvgFill(vg);
}

brls::View* QrCodeView::create() { return new QrCodeView(); }

}  // namespace switch_auto::ui
