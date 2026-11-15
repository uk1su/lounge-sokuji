#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>

class QTcpServer;
class QTcpSocket;

namespace quickscore {

/// Lightweight localhost HTTP server serving overlay.html and live JSON/JS state.
class OverlayServer final : public QObject {
  Q_OBJECT
public:
  explicit OverlayServer(QObject *parent = nullptr);
  ~OverlayServer() override;

  bool start(quint16 preferredPort = 18421);
  void stop();
  quint16 port() const;
  bool isListening() const;

  void setHtmlContent(QByteArray html);
  bool loadHtmlFile(const QString &path);
  QByteArray htmlContent() const { return htmlBytes_; }

  void setState(QByteArray jsonBytes, QByteArray jsBytes);

private:
  void handleNewConnection();
  void handleReadyRead(QTcpSocket *socket);
  void writeResponse(QTcpSocket *socket,
                     int statusCode,
                     const char *reason,
                     const char *contentType,
                     const QByteArray &body,
                     bool headOnly);

  QTcpServer *server_{};
  QByteArray htmlBytes_;
  QByteArray stateJsonBytes_{"{}"};
  QByteArray stateJsBytes_{"window.__QUICKSCORE_OVERLAY__ = {};\n"};
};

} // namespace quickscore
