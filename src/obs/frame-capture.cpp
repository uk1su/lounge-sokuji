#include "frame-capture.hpp"

#include <cstring>

#include <graphics/vec4.h>

namespace quickscore {

FrameCapture::FrameCapture(Callback callback) : callback_(std::move(callback)) {
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

void FrameCapture::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = true;
}
void FrameCapture::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  stage_ = Stage::Wait;
}
void FrameCapture::tick(void *data, float seconds) {
  static_cast<FrameCapture *>(data)->onTick(seconds);
}

void FrameCapture::onTick(float seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || (!source_ && !captureMain_))
    return;
  elapsed_ += seconds;
  bool needsGraphics = stage_ != Stage::Wait;
  if (stage_ == Stage::Wait) {
    needsGraphics = elapsed_ >= 0.125f;
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
    copy();
    stage_ = Stage::Wait;
    break;
  }
  obs_leave_graphics();
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
  uint32_t width = 0, height = 0;
  if (source) {
    width = obs_source_get_width(source);
    height = obs_source_get_height(source);
  } else {
    obs_video_info video{};
    if (obs_get_video_info(&video)) {
      width = video.base_width;
      height = video.base_height;
    }
  }
  if (!width || !height) {
    if (source)
      obs_source_release(source);
    return false;
  }
  if (width != width_ || height != height_) {
    destroyGraphics();
    width_ = width;
    height_ = height;
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
    obs_source_release(source);
    return false;
  }
  vec4 zero;
  vec4_zero(&zero);
  gs_clear(GS_CLEAR_COLOR, &zero, 0.0f, 0);
  gs_ortho(0.0f, static_cast<float>(width_), 0.0f, static_cast<float>(height_), -100.0f, 100.0f);
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

bool FrameCapture::copy() {
  uint8_t *data = nullptr;
  uint32_t stride = 0;
  if (!gs_stagesurface_map(stagesurface_, &data, &stride))
    return false;
  Frame frame;
  frame.width = static_cast<int>(width_);
  frame.height = static_cast<int>(height_);
  frame.stride = static_cast<int>(width_ * 4);
  frame.rgba.resize(static_cast<size_t>(frame.stride * frame.height));
  for (uint32_t y = 0; y < height_; ++y)
    std::memcpy(frame.rgba.data() + static_cast<size_t>(y) * frame.stride,
                data + static_cast<size_t>(y) * stride, frame.stride);
  gs_stagesurface_unmap(stagesurface_);
  if (callback_)
    callback_(std::move(frame));
  return true;
}

} // namespace quickscore
