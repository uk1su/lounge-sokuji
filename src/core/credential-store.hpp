#pragma once
#include <QString>
namespace quickscore {
QString loadStoredCredential(const QString &profile, const QString &service);
bool saveStoredCredential(const QString &profile, const QString &service, const QString &secret,
                          QString &error);
} // namespace quickscore
