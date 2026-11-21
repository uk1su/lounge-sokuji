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

void onFrontendEvent(enum obs_frontend_event event, void *)
{
  if (!g_dock) return;
  if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
      event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
    QMetaObject::invokeMethod(g_dock, &quickscore::MainDock::refreshSources, Qt::QueuedConnection);
  }
}
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
  obs_frontend_add_event_callback(onFrontendEvent, nullptr);
  blog(LOG_INFO, "[MK8DX QuickScore] loaded (%s TypeSafe Jev API, course + result captures)",
       QUICKSCORE_VERSION);
  return true;
}

void obs_module_unload(void)
{
  obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
  if (g_dock) obs_frontend_remove_dock("mk8dx_quickscore_dock");
  g_dock = nullptr;
  blog(LOG_INFO, "[MK8DX QuickScore] unloaded");
}
