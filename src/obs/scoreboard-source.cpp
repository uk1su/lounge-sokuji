#include "scoreboard-source.hpp"

#include <mutex>

#include <obs-module.h>

namespace quickscore {
namespace {
struct SharedImage {
  std::mutex mutex;
  int width{840};
  int height{160};
  uint64_t revision{};
  std::vector<uint8_t> rgba;
} g_image;

struct ScoreboardSource {
  gs_texture_t *texture{};
  uint64_t revision{};
  int width{840};
  int height{160};
};

const char *getName(void *) { return obs_module_text("ScoreboardSource"); }
void *create(obs_data_t *, obs_source_t *) { return new ScoreboardSource; }
void destroy(void *data)
{
  auto *source = static_cast<ScoreboardSource *>(data);
  obs_enter_graphics();
  gs_texture_destroy(source->texture);
  obs_leave_graphics();
  delete source;
}
uint32_t width(void *data) { return static_cast<uint32_t>(static_cast<ScoreboardSource *>(data)->width); }
uint32_t height(void *data) { return static_cast<uint32_t>(static_cast<ScoreboardSource *>(data)->height); }
void render(void *data, gs_effect_t *)
{
  auto *source = static_cast<ScoreboardSource *>(data);
  std::lock_guard<std::mutex> lock(g_image.mutex);
  if (source->revision != g_image.revision && !g_image.rgba.empty()) {
    if (!source->texture || source->width != g_image.width || source->height != g_image.height) {
      gs_texture_destroy(source->texture);
      source->width = g_image.width;
      source->height = g_image.height;
      const uint8_t *pixels = g_image.rgba.data();
      source->texture = gs_texture_create(source->width, source->height, GS_RGBA, 1, &pixels, GS_DYNAMIC);
    } else {
      gs_texture_set_image(source->texture, g_image.rgba.data(), static_cast<uint32_t>(source->width * 4), false);
    }
    source->revision = g_image.revision;
  }
  if (source->texture) obs_source_draw(source->texture, 0, 0, 0, 0, false);
}

obs_source_info sourceInfo = [] {
  obs_source_info info{};
  info.id = "mk8dx_quickscore_scoreboard";
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
  info.get_name = getName;
  info.create = create;
  info.destroy = destroy;
  info.get_width = width;
  info.get_height = height;
  info.video_render = render;
  return info;
}();
} // namespace

void registerScoreboardSource() { obs_register_source(&sourceInfo); }
void updateScoreboardImage(int width, int height, std::vector<uint8_t> rgba)
{
  std::lock_guard<std::mutex> lock(g_image.mutex);
  g_image.width = width;
  g_image.height = height;
  g_image.rgba = std::move(rgba);
  ++g_image.revision;
}

} // namespace quickscore
