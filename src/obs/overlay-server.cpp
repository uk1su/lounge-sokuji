#include "overlay-server.hpp"

#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace quickscore {
namespace {

constexpr const char kBufferProperty[] = "quickscore_http_buf";

QString extractRequestTarget(const QByteArray &requestLine) {
  const QList<QByteArray> parts = requestLine.trimmed().split(' ');
  if (parts.size() < 2)
    return QString();
  QString target = QString::fromUtf8(parts[1]);
  const int queryPos = target.indexOf('?');
  if (queryPos >= 0)
    target.truncate(queryPos);
  return target;
}

} // namespace

OverlayServer::OverlayServer(QObject *parent)
    : QObject(parent), server_(new QTcpServer(this)) {
  connect(server_, &QTcpServer::newConnection, this, &OverlayServer::handleNewConnection);
}

OverlayServer::~OverlayServer() { stop(); }

bool OverlayServer::start(quint16 preferredPort) {
  if (server_->isListening())
    return true;
  if (preferredPort > 0 && server_->listen(QHostAddress::LocalHost, preferredPort))
    return true;
  return server_->listen(QHostAddress::LocalHost, 0);
}

void OverlayServer::stop() {
  if (server_->isListening())
    server_->close();
}

quint16 OverlayServer::port() const { return server_->serverPort(); }

bool OverlayServer::isListening() const { return server_->isListening(); }

void OverlayServer::setHtmlContent(QByteArray html) { htmlBytes_ = std::move(html); }

bool OverlayServer::loadHtmlFile(const QString &path) {
  if (path.isEmpty())
    return false;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  htmlBytes_ = file.readAll();
  return !htmlBytes_.isEmpty();
}

void OverlayServer::setState(QByteArray jsonBytes, QByteArray jsBytes) {
  stateJsonBytes_ = std::move(jsonBytes);
  stateJsBytes_ = std::move(jsBytes);
}

void OverlayServer::handleNewConnection() {
  while (QTcpSocket *socket = server_->nextPendingConnection()) {
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
  }
}

void OverlayServer::handleReadyRead(QTcpSocket *socket) {
  QByteArray buf = socket->property(kBufferProperty).toByteArray();
  buf.append(socket->readAll());
  if (buf.size() > 16384) {
    socket->disconnectFromHost();
    return;
  }

  const int headerEnd = buf.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    socket->setProperty(kBufferProperty, buf);
    return;
  }

  const int lineEnd = buf.indexOf("\r\n");
  const QByteArray requestLine = lineEnd >= 0 ? buf.left(lineEnd) : buf.left(headerEnd);
  const QList<QByteArray> parts = requestLine.trimmed().split(' ');
  const QByteArray method = parts.isEmpty() ? QByteArray() : parts[0].toUpper();
  const bool headOnly = (method == "HEAD");
  if (method != "GET" && method != "HEAD") {
    writeResponse(socket, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                  "Method Not Allowed", false);
    return;
  }

  const QString path = extractRequestTarget(requestLine);
  if (path == QStringLiteral("/") || path == QStringLiteral("/overlay.html")) {
    writeResponse(socket, 200, "OK", "text/html; charset=utf-8", htmlBytes_, headOnly);
  } else if (path == QStringLiteral("/state.json")) {
    writeResponse(socket, 200, "OK", "application/json; charset=utf-8", stateJsonBytes_, headOnly);
  } else if (path == QStringLiteral("/overlay-state.js")) {
    writeResponse(socket, 200, "OK", "application/javascript; charset=utf-8", stateJsBytes_,
                  headOnly);
  } else {
    writeResponse(socket, 404, "Not Found", "text/plain; charset=utf-8", "Not Found", headOnly);
  }
}

void OverlayServer::writeResponse(QTcpSocket *socket,
                                  int statusCode,
                                  const char *reason,
                                  const char *contentType,
                                  const QByteArray &body,
                                  bool headOnly) {
  QByteArray header;
  header.append("HTTP/1.1 ");
  header.append(QByteArray::number(statusCode));
  header.append(' ');
  header.append(reason);
  header.append("\r\nContent-Type: ");
  header.append(contentType);
  header.append("\r\nContent-Length: ");
  header.append(QByteArray::number(body.size()));
  header.append("\r\nCache-Control: no-store, max-age=0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");

  socket->write(header);
  if (!headOnly && !body.isEmpty())
    socket->write(body);
  socket->disconnectFromHost();
}

} // namespace quickscore
