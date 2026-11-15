#pragma once
#include "scoring.hpp"
#include <QJsonArray>
#include <QString>
namespace quickscore {
bool loadSession(const QString &path, Session &session, QJsonArray &audit, QString &error);
bool storeSession(const QString &path, const Session &session, const QJsonArray &audit,
                  QString &error);
} // namespace quickscore
