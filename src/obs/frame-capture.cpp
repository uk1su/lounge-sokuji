#include "frame-capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <graphics/vec4.h>

namespace quickscore {

FrameCapture::FrameCapture(Callback callback, ShouldCaptureCallback shouldCapture)
    : callback_(std::move(callback)), shouldCapture_(std::move(shouldCapture)) {
  obs_add_tick_callback(tick, this);
}

FrameCapture::~FrameCapture() {
  obs_remove_tick_callback(tick, this);
  if (source_)
    obs_weak_source_release(source_);
  obs_enter_graphics();
  destroyGraphics();
  obs_leave_graphics();
}

void FrameCapture::setSource(obs_source_t *source) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (source_)
    obs_weak_source_release(source_);
  source_ = source ? obs_source_get_weak_source(source) : nullptr;
  captureMain_ = source == nullptr;
  stage_ = Stage::Wait;
  elapsed_ = 0.0f;
}

void FrameCapture::setIntervalSeconds(float seconds) {
  interval_.store(std::clamp(seconds, 0.050f, 2.0f), std::memory_order_relaxed);
}

void FrameCapture::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  elapsed_ = interval_.load(std::memory_order_relaxed);
  stage_ = Stage::Wait;
  running_.store(true, std::memory_order_release);
}
void FrameCapture::stop() {
  running_.store(false, std::memory_order_release);
  std::lock_guard<std::mutex> lock(mutex_);
  stage_ = Stage::Wait;
}
void FrameCapture::tick(void *data, float seconds) {
  static_cast<FrameCapture *>(data)->onTick(seconds);
}

void FrameCapture::onTick(float seconds) {
  if (!running_.load(std::memory_order_acquire))
    return;

  Frame captured;
  bool hasCaptured = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load(std::memory_order_relaxed) || (!source_ && !captureMain_))
      return;
    elapsed_ += seconds;
    bool needsGraphics = stage_ != Stage::Wait;
    if (stage_ == Stage::Wait) {
      needsGraphics = elapsed_ >= interval_.load(std::memory_order_relaxed);
      if (needsGraphics && shouldCapture_ && !shouldCapture_())
        return;
    }
    if (!needsGraphics)
      return;

    obs_enter_graphics();
    switch (stage_) {
    case Stage::Wait:
      elapsed_ = 0.0f;
      if (render())
        stage_ = Stage::Rendered;
      break;
    case Stage::Rendered:
      if (download())
        stage_ = Stage::Downloaded;
      else
        stage_ = Stage::Wait;
      break;
    case Stage::Downloaded:
      hasCaptured = copy(captured);
      stage_ = Stage::Wait;
      break;
    }
    obs_leave_graphics();
  }
  if (hasCaptured && callback_)
    callback_(std::move(captured));
}

void FrameCapture::destroyGraphics() {
  gs_stagesurface_destroy(stagesurface_);
  gs_texrender_destroy(texrender_);
  stagesurface_ = nullptr;
  texrender_ = nullptr;
  width_ = height_ = 0;
}

bool FrameCapture::render() {
  obs_source_t *source = source_ ? obs_weak_source_get_source(source_) : nullptr;
  if (!source && !captureMain_)
    return false;
  uint32_t srcWidth = 0, srcHeight = 0;
  if (source) {
    srcWidth = obs_source_get_width(source);
    srcHeight = obs_source_get_height(source);
  } else {
    obs_video_info video{};
    if (obs_get_video_info(&video)) {
      srcWidth = video.base_width;
      srcHeight = video.base_height;
    }
  }
  if (!srcWidth || !srcHeight) {
    if (source)
      obs_source_release(source);
    return false;
  }
  uint32_t targetWidth = srcWidth;
  uint32_t targetHeight = srcHeight;
  if ((srcWidth > 1920 || srcHeight > 1080) &&
      std::abs(double(srcWidth) / double(srcHeight) - 16.0 / 9.0) <= 0.03) {
    targetWidth = 1920;
    targetHeight = 1080;
  }
  if (targetWidth != width_ || targetHeight != height_) {
    destroyGraphics();
    width_ = targetWidth;
    height_ = targetHeight;
    texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    stagesurface_ = gs_stagesurface_create(width_, height_, GS_RGBA);
  }
  if (!texrender_ || !stagesurface_) {
    if (source)
      obs_source_release(source);
    return false;
  }
  gs_texrender_reset(texrender_);
  if (!gs_texrender_begin(texrender_, width_, height_)) {
    if (source)
      obs_source_release(source);
    return false;
  }
  vec4 zero;
  vec4_zero(&zero);
  gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
  gs_ortho(0.0f, static_cast<float>(srcWidth), 0.0f, static_cast<float>(srcHeight), -100.0f, 100.0f);
  gs_blend_state_push();
  gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
  if (source) {
    obs_source_inc_showing(source);
    obs_source_video_render(source);
    obs_source_dec_showing(source);
  } else {
    obs_render_main_texture();
  }
  gs_blend_state_pop();
  gs_texrender_end(texrender_);
  if (source)
    obs_source_release(source);
  return true;
}

bool FrameCapture::download() {
  if (!stagesurface_ || !texrender_)
    return false;
  gs_stage_texture(stagesurface_, gs_texrender_get_texture(texrender_));
  return true;
}

bool FrameCapture::copy(Frame &out) {
  uint8_t *data = nullptr;
  uint32_t stride = 0;
  if (!gs_stagesurface_map(stagesurface_, &data, &stride))
    return false;
  out.width = static_cast<int>(width_);
  out.height = static_cast<int>(height_);
  out.stride = static_cast<int>(width_ * 4);
  const size_t totalBytes = static_cast<size_t>(out.stride) * static_cast<size_t>(out.height);
  out.rgba = pool_->acquire(totalBytes);
  if (stride == static_cast<uint32_t>(out.stride)) {
    std::memcpy(out.rgba.data(), data, out.rgba.size());
  } else {
    for (uint32_t y = 0; y < height_; ++y)
      std::memcpy(out.rgba.data() + static_cast<size_t>(y) * out.stride,
                  data + static_cast<size_t>(y) * stride, out.stride);
  }
  gs_stagesurface_unmap(stagesurface_);
  return true;
}

} // namespace quickscore
