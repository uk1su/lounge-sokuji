#pragma once

#include "vision.hpp"

#include <functional>
#include <mutex>

#include <obs.h>

namespace quickscore {

class FrameCapture {
public:
  using Callback = std::function<void(Frame)>;
  explicit FrameCapture(Callback callback);
  ~FrameCapture();
  FrameCapture(const FrameCapture &) = delete;
  FrameCapture &operator=(const FrameCapture &) = delete;

  void setSource(obs_source_t *source);
  void start();
  void stop();
  bool running() const { std::lock_guard<std::mutex> lock(mutex_); return running_; }

private:
  enum class Stage { Wait, Rendered, Downloaded };
  static void tick(void *data, float seconds);
  void onTick(float seconds);
  bool render();
  bool download();
  bool copy();
  void destroyGraphics();

  Callback callback_;
  mutable std::mutex mutex_;
  obs_weak_source_t *source_{};
  bool captureMain_{};
  gs_texrender_t *texrender_{};
  gs_stagesurf_t *stagesurface_{};
  uint32_t width_{};
  uint32_t height_{};
  float elapsed_{};
  bool running_{};
  Stage stage_{Stage::Wait};
};

} // namespace quickscore
