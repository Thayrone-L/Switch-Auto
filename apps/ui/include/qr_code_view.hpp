#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>

namespace qrcodegen { class QrCode; }

namespace switch_auto::ui {

class QrCodeView final : public brls::View {
public:
    QrCodeView();
    ~QrCodeView() override;

    void setPayload(const std::string& payload);
    void draw(NVGcontext* vg, float x, float y, float width, float height,
        brls::Style style, brls::FrameContext* ctx) override;
    static brls::View* create();

private:
    std::unique_ptr<qrcodegen::QrCode> code_;
};

}  // namespace switch_auto::ui
