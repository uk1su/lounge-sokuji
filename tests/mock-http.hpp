#pragma once
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
#include <stdexcept>
class MockServer {
public:
  QTcpServer server;
  QByteArray body;
  QByteArray request;
  int status = 200, count = 0, delay = 0;
  MockServer() {
    if (!server.listen(QHostAddress::LocalHost, 0))
      throw std::runtime_error("mock server listen");
    QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
      while (auto *socket = server.nextPendingConnection()) {
        auto data = std::make_shared<QByteArray>();
        auto sent = std::make_shared<bool>(false);
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, data, sent] {
          *data += socket->readAll();
          const int separator = data->indexOf("\r\n\r\n");
          if (*sent || separator < 0)
            return;
          qint64 length = 0;
          for (const auto &line : data->left(separator).split('\n'))
            if (line.toLower().startsWith("content-length:"))
              length = line.mid(15).trimmed().toLongLong();
          if (data->size() - separator - 4 < length)
            return;
          *sent = true;
          ++count;
          request = *data;
          const auto response = "HTTP/1.1 " + QByteArray::number(status) +
                                " Test\r\n"
                                "Content-Type: application/json\r\nConnection: close\r\n"
                                "Location: http://127.0.0.1:" +
                                QByteArray::number(server.serverPort()) +
                                "/redirect\r\n"
                                "Content-Length: " +
                                QByteArray::number(body.size()) + "\r\n\r\n" + body;
          QTimer::singleShot(delay, socket, [socket, response] {
            if (socket->state() == QAbstractSocket::ConnectedState) {
              socket->write(response);
              socket->disconnectFromHost();
            }
          });
        });
      }
    });
  }
  QUrl url() const {
    return QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/test");
  }
};
