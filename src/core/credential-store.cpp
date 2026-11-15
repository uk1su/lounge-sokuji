#include "credential-store.hpp"
#include <QCryptographicHash>
#include <QDir>
#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <wincred.h>
// clang-format on
#endif
namespace quickscore {
namespace {
QString credentialTarget(const QString &profile, const QString &service) {
  const auto id =
      QCryptographicHash::hash(QDir(profile).absolutePath().toUtf8(), QCryptographicHash::Sha256)
          .toHex();
  return "MK8DX QuickScore/" + service + "/" + QString::fromLatin1(id);
}
} // namespace
QString loadStoredCredential(const QString &profile, const QString &service) {
#ifdef _WIN32
  PCREDENTIALW credential = nullptr;
  const auto target = credentialTarget(profile, service).toStdWString();
  if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    const auto secret =
        QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                          int(credential->CredentialBlobSize));
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return secret;
  }
#endif
  return {};
}
bool saveStoredCredential(const QString &profile, const QString &service, const QString &secret,
                          QString &error) {
  error.clear();
#ifdef _WIN32
  const auto target = credentialTarget(profile, service).toStdWString();
  auto bytes = secret.trimmed().toUtf8();
  if (bytes.isEmpty()) {
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
      error = QStringLiteral("保存したBotトークンを削除できません。");
      return false;
    }
    return true;
  }
  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = const_cast<wchar_t *>(target.c_str());
  credential.CredentialBlobSize = DWORD(bytes.size());
  credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  const bool ok = bytes.size() <= CRED_MAX_CREDENTIAL_BLOB_SIZE && CredWriteW(&credential, 0);
  SecureZeroMemory(bytes.data(), size_t(bytes.size()));
  if (!ok)
    error = QStringLiteral("Windows資格情報マネージャーへBotトークンを保存できません。");
  return ok;
#else
  error = QStringLiteral("Botトークン保存はWindows版のみ対応しています。");
  return false;
#endif
}
} // namespace quickscore
