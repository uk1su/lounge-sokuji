#pragma once

#include "vision.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <obs.h>

namespace quickscore {

class FrameBufferPool {
public:
  std::vector<uint8_t> acquire(size_t bytes) {
    std::vector<uint8_t> buf;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!freeList_.empty()) {
        buf = std::move(freeList_.back());
        freeList_.pop_back();
      }
    }
    buf.resize(bytes);
    return buf;
  }

  void release(std::vector<uint8_t> &&buf) {
    if (buf.capacity() == 0)
      return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (freeList_.size() < 4) {
      buf.clear();
      freeList_.push_back(std::move(buf));
    }
  }

private:
  std::mutex mutex_;
  std::vector<std::vector<uint8_t>> freeList_;
};

class FrameCapture {
public:
  using Callback = std::function<void(Frame)>;
  using ShouldCaptureCallback = std::function<bool()>;
  explicit FrameCapture(Callback callback, ShouldCaptureCallback shouldCapture = {});
  ~FrameCapture();
  FrameCapture(const FrameCapture &) = delete;
  FrameCapture &operator=(const FrameCapture &) = delete;

  void setSource(obs_source_t *source);
  void setIntervalSeconds(float seconds);
  std::shared_ptr<FrameBufferPool> bufferPool() const { return pool_; }
  void start();
  void stop();
  bool running() const { return running_.load(std::memory_order_acquire); }

private:
  enum class Stage { Wait, Rendered, Downloaded };
  static void tick(void *data, float seconds);
  void onTick(float seconds);
  bool render();
  bool download();
  bool copy(Frame &out);
  void destroyGraphics();

  Callback callback_;
  ShouldCaptureCallback shouldCapture_;
  std::shared_ptr<FrameBufferPool> pool_{std::make_shared<FrameBufferPool>()};
  mutable std::mutex mutex_;
  obs_weak_source_t *source_{};
  bool captureMain_{};
  gs_texrender_t *texrender_{};
  gs_stagesurf_t *stagesurface_{};
  uint32_t width_{};
  uint32_t height_{};
  float elapsed_{};
  std::atomic<float> interval_{0.250f};
  std::atomic<bool> running_{false};
  Stage stage_{Stage::Wait};
};

} // namespace quickscore
