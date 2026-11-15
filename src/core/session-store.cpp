#include "session-store.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <set>
namespace quickscore {
bool storeSession(const QString &path, const Session &session, const QJsonArray &audit,
                  QString &error) {
  auto root = QJsonDocument::fromJson(QByteArray::fromStdString(session.toJson())).object();
  root.insert("schema", 2);
  root.insert("recognitionAudit", audit);
  const auto data = QJsonDocument(root).toJson();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
    error = file.errorString();
    return false;
  }
  return true;
}
bool loadSession(const QString &path, Session &session, QJsonArray &audit, QString &error) {
  QFile file(path);
  if (!file.exists())
    return true;
  auto fail = [&] {
    error = QStringLiteral("session.jsonを復元できません。元ファイルを確認してください。");
    return false;
  };
  if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
    return fail();
  QJsonParseError parse;
  auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject())
    return fail();
  const auto root = doc.object();
  if (!root.value("teams").isArray() || !root.value("races").isArray())
    return fail();
  const int players = root.value("playersPerTeam").toInt();
  if (players != 2 && players != 3 && players != 4 && players != 6)
    return fail();
  std::vector<std::string> tags;
  std::set<std::string> unique;
  for (const auto &v : root.value("teams").toArray()) {
    auto tag = v.toString().toStdString();
    if (tag.empty() || !unique.insert(tag).second)
      return fail();
    tags.push_back(tag);
  }
  const auto races = root.value("races").toArray();
  if ((!tags.empty() || !races.empty()) && int(tags.size()) * players != 12)
    return fail();
  Session restored;
  restored.configure(tags, players);
  for (const auto &v : races) {
    const auto race = v.toObject();
    std::vector<Standing> standings;
    if (!race.value("standings").isArray())
      return fail();
    for (const auto &s : race.value("standings").toArray()) {
      const auto obj = s.toObject();
      const double confidence = obj.value("confidence").toDouble(-1);
      if (!std::isfinite(confidence) || confidence < 0 || confidence > 1)
        return fail();
      standings.push_back(
          {obj.value("rank").toInt(), obj.value("tag").toString().toStdString(), confidence});
    }
    if (!restored.commit(std::move(standings), race.value("corrected").toBool()))
      return fail();
  }
  session = std::move(restored);
  audit = root.value("recognitionAudit").toArray();
  return true;
}
} // namespace quickscore
