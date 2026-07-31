#include "android_auto_video_view.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <mutex>
#include <utility>

#ifdef __SWITCH__
#include <glad/glad.h>

extern "C" int nvglCreateImageFromHandleGL3(
    NVGcontext* ctx, GLuint textureId, int width, int height, int flags);
#endif

namespace switch_auto::ui {
namespace {

std::mutex frameMutex;
std::vector<std::uint8_t> pendingFrame;
std::vector<std::uint8_t> recycledFrame;
AndroidAutoVideoView::Yuv420Frame pendingYuvFrame;
AndroidAutoVideoView::Yuv420Frame recycledYuvFrame;
bool pendingFrameIsYuv = false;
int pendingWidth = 0;
int pendingHeight = 0;
std::uint64_t pendingGeneration = 0;
std::atomic<bool> framePending{false};
AndroidAutoVideoView::TouchHandler touchHandler;

#ifdef __SWITCH__
GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;

    GLchar error[512]{};
    glGetShaderInfoLog(shader, sizeof(error), nullptr, error);
    brls::Logger::error("Android Auto YUV shader compile failed: {}", error);
    glDeleteShader(shader);
    return 0;
}

GLuint createYuvProgram() {
    static constexpr const char* vertexSource = R"(
#version 330 core
out vec2 textureCoordinate;
void main() {
    vec2 position = vec2(
        float((gl_VertexID << 1) & 2),
        float(gl_VertexID & 2));
    textureCoordinate = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";
    static constexpr const char* fragmentSource = R"(
#version 330 core
in vec2 textureCoordinate;
out vec4 outputColor;
uniform sampler2D lumaTexture;
uniform sampler2D chromaUTexture;
uniform sampler2D chromaVTexture;
void main() {
    float y = texture(lumaTexture, textureCoordinate).r;
    float u = texture(chromaUTexture, textureCoordinate).r - 0.5;
    float v = texture(chromaVTexture, textureCoordinate).r - 0.5;
    float limitedY = 1.16438356 * (y - 0.06274510);
    vec3 rgb = vec3(
        limitedY + 1.79274107 * v,
        limitedY - 0.21324861 * u - 0.53290933 * v,
        limitedY + 2.11240179 * u);
    outputColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return program;

    GLchar error[512]{};
    glGetProgramInfoLog(program, sizeof(error), nullptr, error);
    brls::Logger::error("Android Auto YUV program link failed: {}", error);
    glDeleteProgram(program);
    return 0;
}
#endif

}  // namespace

AndroidAutoVideoView::AndroidAutoVideoView() {
}

AndroidAutoVideoView::~AndroidAutoVideoView() = default;

#ifdef __SWITCH__
bool AndroidAutoVideoView::ensureYuvRenderer(
    NVGcontext* vg, int width, int height) {
    if (!yuvProgram_) yuvProgram_ = createYuvProgram();
    if (!yuvProgram_) return false;
    if (!yuvFramebuffer_) glGenFramebuffers(1, &yuvFramebuffer_);
    if (!yuvVertexArray_) glGenVertexArrays(1, &yuvVertexArray_);
    if (!yuvFramebuffer_ || !yuvVertexArray_) return false;

    if (yuvWidth_ == width && yuvHeight_ == height &&
        yuvPlaneTextures_[0] && yuvOutputTextures_[0] &&
        yuvNanoImages_[0]) {
        return true;
    }

    for (int& image : yuvNanoImages_) {
        if (image) nvgDeleteImage(vg, image);
        image = 0;
    }
    glDeleteTextures(3, yuvPlaneTextures_);
    glDeleteTextures(2, yuvOutputTextures_);
    std::fill(std::begin(yuvPlaneTextures_),
        std::end(yuvPlaneTextures_), 0U);
    std::fill(std::begin(yuvOutputTextures_),
        std::end(yuvOutputTextures_), 0U);

    glGenTextures(3, yuvPlaneTextures_);
    const int planeWidths[3]{width, width / 2, width / 2};
    const int planeHeights[3]{height, height / 2, height / 2};
    for (int plane = 0; plane < 3; ++plane) {
        glBindTexture(GL_TEXTURE_2D, yuvPlaneTextures_[plane]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
            planeWidths[plane], planeHeights[plane], 0,
            GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glGenTextures(2, yuvOutputTextures_);
    for (int index = 0; index < 2; ++index) {
        glBindTexture(GL_TEXTURE_2D, yuvOutputTextures_[index]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
            width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // NanoVG must not own the GL handle; the view keeps both output
        // textures alive for double-buffered conversion.
        yuvNanoImages_[index] = nvglCreateImageFromHandleGL3(
            vg, yuvOutputTextures_[index], width, height, 1 << 16);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    yuvWidth_ = width;
    yuvHeight_ = height;
    yuvDisplayedTexture_ = 0;
    return yuvPlaneTextures_[0] && yuvOutputTextures_[0] &&
        yuvNanoImages_[0] && yuvNanoImages_[1];
}

bool AndroidAutoVideoView::updateYuvTexture(
    NVGcontext* vg, const Yuv420Frame& frame) {
    if (!ensureYuvRenderer(vg, frame.width, frame.height)) return false;

    GLint previousFramebuffer = 0;
    GLint previousViewport[4]{};
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int planeWidths[3]{
        frame.width, frame.width / 2, frame.width / 2};
    const int planeHeights[3]{
        frame.height, frame.height / 2, frame.height / 2};
    const auto lumaSize =
        static_cast<std::size_t>(frame.width) * frame.height;
    const std::size_t planeOffsets[3]{
        0, lumaSize, lumaSize + lumaSize / 4};
    for (int plane = 0; plane < 3; ++plane) {
        glActiveTexture(GL_TEXTURE0 + plane);
        glBindTexture(GL_TEXTURE_2D, yuvPlaneTextures_[plane]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
            planeWidths[plane], planeHeights[plane],
            GL_RED, GL_UNSIGNED_BYTE,
            frame.data.data() + planeOffsets[plane]);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    const int outputIndex = 1 - yuvDisplayedTexture_;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, yuvFramebuffer_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, yuvOutputTextures_[outputIndex], 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousFramebuffer);
        glViewport(previousViewport[0], previousViewport[1],
            previousViewport[2], previousViewport[3]);
        return false;
    }

    glViewport(0, 0, frame.width, frame.height);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(yuvProgram_);
    glUniform1i(
        glGetUniformLocation(yuvProgram_, "lumaTexture"), 0);
    glUniform1i(
        glGetUniformLocation(yuvProgram_, "chromaUTexture"), 1);
    glUniform1i(
        glGetUniformLocation(yuvProgram_, "chromaVTexture"), 2);
    glBindVertexArray(yuvVertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousFramebuffer);
    glViewport(previousViewport[0], previousViewport[1],
        previousViewport[2], previousViewport[3]);
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glActiveTexture(GL_TEXTURE0);
    yuvDisplayedTexture_ = outputIndex;
    return true;
}
#endif

void AndroidAutoVideoView::dispatchTouch(
    AndroidAutoTouchAction action, brls::Point position) {
    const brls::Rect frame = getFrame();
    if (frame.size.width <= 0 || frame.size.height <= 0) return;

    AndroidAutoTouchEvent event;
    event.action = action;
    event.normalizedX = std::clamp(
        (position.x - frame.origin.x) / frame.size.width, 0.0f, 1.0f);
    event.normalizedY = std::clamp(
        (position.y - frame.origin.y) / frame.size.height, 0.0f, 1.0f);
    // Android Auto timestamps input against a monotonic clock. system/high
    // resolution epoch time is in a different domain and makes Gearhead defer
    // or reorder an otherwise immediately delivered gesture.
    event.timestampMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    TouchHandler handler;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        handler = touchHandler;
    }
    if (handler) handler(event);
}

void AndroidAutoVideoView::submitFrame(
    int width, int height, std::vector<std::uint8_t> rgba) {
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        return;
    }
    std::lock_guard<std::mutex> lock(frameMutex);
    pendingWidth = width;
    pendingHeight = height;
    pendingFrame = std::move(rgba);
    pendingYuvFrame = {};
    pendingFrameIsYuv = false;
    ++pendingGeneration;
    framePending.store(true, std::memory_order_release);
}

std::vector<std::uint8_t> AndroidAutoVideoView::acquireFrameBuffer(
    std::size_t requiredSize) {
    std::vector<std::uint8_t> buffer;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        buffer = std::move(recycledFrame);
    }
    buffer.resize(requiredSize);
    return buffer;
}

AndroidAutoVideoView::Yuv420Frame
AndroidAutoVideoView::acquireYuv420Frame(int width, int height) {
    Yuv420Frame frame;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        frame = std::move(recycledYuvFrame);
    }
    frame.width = width;
    frame.height = height;
    const auto lumaSize = static_cast<std::size_t>(width) * height;
    frame.data.resize(lumaSize + lumaSize / 2);
    return frame;
}

void AndroidAutoVideoView::submitYuv420Frame(Yuv420Frame frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        (frame.width & 1) != 0 || (frame.height & 1) != 0) {
        return;
    }
    const auto lumaSize =
        static_cast<std::size_t>(frame.width) * frame.height;
    if (frame.data.size() != lumaSize + lumaSize / 2) return;

    std::lock_guard<std::mutex> lock(frameMutex);
    pendingWidth = frame.width;
    pendingHeight = frame.height;
    pendingFrame.clear();
    pendingYuvFrame = std::move(frame);
    pendingFrameIsYuv = true;
    ++pendingGeneration;
    framePending.store(true, std::memory_order_release);
}

bool AndroidAutoVideoView::canAcceptFrame() {
    return !framePending.load(std::memory_order_acquire);
}

void AndroidAutoVideoView::setTouchHandler(TouchHandler handler) {
    std::lock_guard<std::mutex> lock(frameMutex);
    touchHandler = std::move(handler);
}

void AndroidAutoVideoView::draw(NVGcontext* vg, float x, float y,
    float width, float height, brls::Style style,
    brls::FrameContext* ctx) {
    std::vector<std::uint8_t> nextFrame;
    Yuv420Frame nextYuvFrame;
    bool nextIsYuv = false;
    int nextWidth = 0;
    int nextHeight = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (pendingGeneration != displayedGeneration_) {
            // Transfer ownership to the render thread. Copying a 1280x720 RGBA
            // frame here cost roughly 3.5 MiB on every displayed frame.
            nextFrame = std::move(pendingFrame);
            nextYuvFrame = std::move(pendingYuvFrame);
            nextIsYuv = pendingFrameIsYuv;
            nextWidth = pendingWidth;
            nextHeight = pendingHeight;
            displayedGeneration_ = pendingGeneration;
            framePending.store(false, std::memory_order_release);
        }
    }

    if (nextIsYuv && !nextYuvFrame.data.empty()) {
#ifdef __SWITCH__
        const auto uploadStarted = std::chrono::steady_clock::now();
        if (updateYuvTexture(vg, nextYuvFrame)) {
            displayingYuv_ = true;
            const auto uploadMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - uploadStarted)
                    .count();
            ++uploadSamples_;
            totalUploadMicros_ +=
                static_cast<std::uint64_t>(uploadMicros);
            maxUploadMicros_ = std::max(maxUploadMicros_,
                static_cast<std::uint64_t>(uploadMicros));
        }
#endif
        std::lock_guard<std::mutex> lock(frameMutex);
        recycledYuvFrame = std::move(nextYuvFrame);
    } else if (!nextFrame.empty()) {
#ifdef __SWITCH__
        displayingYuv_ = false;
#endif
        const auto uploadStarted = std::chrono::steady_clock::now();
        if (!videoTextures_[0] || nextWidth != videoWidth_ ||
            nextHeight != videoHeight_) {
            for (const int texture : videoTextures_) {
                if (texture) nvgDeleteImage(vg, texture);
            }
            videoTextures_[0] = nvgCreateImageRGBA(
                vg, nextWidth, nextHeight, 0, nextFrame.data());
            videoTextures_[1] = nvgCreateImageRGBA(
                vg, nextWidth, nextHeight, 0, nextFrame.data());
            displayedTexture_ = 0;
            videoWidth_ = nextWidth;
            videoHeight_ = nextHeight;
        } else {
            // Upload into the texture that is not currently displayed. This
            // avoids synchronizing the CPU with a texture still in use by the
            // GPU from the previous frame.
            const int alternateTexture = 1 - displayedTexture_;
            const int uploadTexture = videoTextures_[alternateTexture]
                ? alternateTexture
                : displayedTexture_;
            nvgUpdateImage(
                vg, videoTextures_[uploadTexture], nextFrame.data());
            displayedTexture_ = uploadTexture;
        }
        const auto uploadMicros =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - uploadStarted)
                .count();
        ++uploadSamples_;
        totalUploadMicros_ += static_cast<std::uint64_t>(uploadMicros);
        maxUploadMicros_ = std::max(
            maxUploadMicros_, static_cast<std::uint64_t>(uploadMicros));
        // NanoVG consumes the pixels synchronously. Preserve the allocation
        // for the decoder instead of allocating and freeing 3.5 MiB per frame.
        std::lock_guard<std::mutex> lock(frameMutex);
        recycledFrame = std::move(nextFrame);
    }

    if (uploadSamples_ >= 300) {
        brls::Logger::info(
            "Android Auto texture upload: mode={} avg={}ms max={}ms",
#ifdef __SWITCH__
            displayingYuv_ ? "yuv-gpu" : "rgba",
#else
            "rgba",
#endif
            static_cast<double>(totalUploadMicros_) /
                static_cast<double>(uploadSamples_) / 1000.0,
            static_cast<double>(maxUploadMicros_) / 1000.0);
        uploadSamples_ = 0;
        totalUploadMicros_ = 0;
        maxUploadMicros_ = 0;
    }

#ifdef __SWITCH__
    const int displayedImage = displayingYuv_
        ? yuvNanoImages_[yuvDisplayedTexture_]
        : videoTextures_[displayedTexture_];
#else
    const int displayedImage = videoTextures_[displayedTexture_];
#endif
    if (!displayedImage) {
        brls::Image::draw(vg, x, y, width, height, style, ctx);
        return;
    }
    const NVGpaint image = nvgImagePattern(
        vg, x, y, width, height, 0,
        displayedImage, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, image);
    nvgFill(vg);

}

brls::View* AndroidAutoVideoView::create() {
    return new AndroidAutoVideoView();
}

}  // namespace switch_auto::ui
