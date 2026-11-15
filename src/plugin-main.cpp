#include "main-dock.hpp"
#include "scoreboard-source.hpp"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

#include <obs-frontend-api.h>
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("mk8dx-quickscore", "ja-JP")
MODULE_EXPORT const char *obs_module_description(void)
{
  return "Mario Kart 8 Deluxe in-OBS result aggregation";
}

namespace {
quickscore::MainDock *g_dock{};
}

bool obs_module_load(void)
{
  // Ship the Schannel TLS backend beside the DLL; use OBS's existing Qt6Network.dll.
  QCoreApplication::addLibraryPath(QFileInfo(QString::fromUtf8(
      obs_get_module_binary_path(obs_current_module()))).absolutePath());
  quickscore::registerScoreboardSource();
  char *path = obs_module_config_path("");
  const QString configDirectory = QString::fromUtf8(path ? path : "");
  bfree(path);
  QDir().mkpath(configDirectory);
  g_dock = new quickscore::MainDock(configDirectory);
  const QString dockTitle = QString("%1 (v%2)")
      .arg(QString::fromUtf8(obs_module_text("DockTitle")),
           QStringLiteral(QUICKSCORE_VERSION));
  if (!obs_frontend_add_dock_by_id("mk8dx_quickscore_dock", dockTitle.toUtf8().constData(), g_dock)) {
    blog(LOG_ERROR, "[MK8DX QuickScore] Failed to add dock");
    delete g_dock;
    g_dock = nullptr;
    return false;
  }
  blog(LOG_INFO, "[MK8DX QuickScore] loaded (%s TypeSafe Jev API, course + result captures)",
       QUICKSCORE_VERSION);
  return true;
}

void obs_module_unload(void)
{
  if (g_dock) obs_frontend_remove_dock("mk8dx_quickscore_dock");
  g_dock = nullptr;
  blog(LOG_INFO, "[MK8DX QuickScore] unloaded");
}
