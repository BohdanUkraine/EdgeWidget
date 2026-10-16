#include "device/DeviceLink.h"

#include <QDebug>

namespace {
constexpr quint16 kEmulatorPort = 47800;
constexpr int kRetryMs = 2000;
} // namespace

DeviceLink::DeviceLink(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_retry.stop();
        emit connectedChanged(true);
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        m_reader = {};
        emit connectedChanged(false);
        m_retry.start();
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (!isConnected())
            m_retry.start();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] {
        for (const MediaSpin::Message &message : m_reader.feed(m_socket.readAll())) {
            qDebug().noquote() << "[device] <-" << MediaSpin::describe(message);
            emit messageReceived(message);
        }
    });

    m_retry.setInterval(kRetryMs);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &DeviceLink::tryConnect);
    tryConnect();
}

void DeviceLink::tryConnect()
{
    if (m_socket.state() == QAbstractSocket::UnconnectedState)
        m_socket.connectToHost(QHostAddress::LocalHost, kEmulatorPort);
}

void DeviceLink::send(const MediaSpin::Message &message)
{
    if (!isConnected())
        return;
    qDebug().noquote() << "[device] ->" << MediaSpin::describe(message);
    m_socket.write(MediaSpin::frame(message));
}
