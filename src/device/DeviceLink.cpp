#include "device/DeviceLink.h"

#include "device/BleLink.h"

#include <QDebug>

namespace {
constexpr quint16 kEmulatorPort = 47800;
constexpr int kRetryMs = 2000;
} // namespace

DeviceLink::DeviceLink(QObject *parent)
    : QObject(parent)
    , m_ble(new BleLink(this))
{
    connect(m_ble, &BleLink::connectedChanged, this, &DeviceLink::update);
    connect(m_ble, &BleLink::messageReceived, this, [this](const MediaSpin::Message &message) {
        if (m_via == Via::Ble)
            emit messageReceived(message);
    });

    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_retry.stop();
        qDebug() << "[device] emulator connected";
        update();
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        m_reader = {};
        update();
        m_retry.start();
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (!emulatorUp())
            m_retry.start();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] {
        for (const MediaSpin::Message &message : m_reader.feed(m_socket.readAll())) {
            qDebug().noquote() << "[device] <-" << MediaSpin::describe(message);
            if (m_via == Via::Emulator)
                emit messageReceived(message);
        }
    });

    m_retry.setInterval(kRetryMs);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &DeviceLink::tryConnect);
    tryConnect();
}

bool DeviceLink::isConnected() const
{
    return m_via != Via::None;
}

void DeviceLink::update()
{
    const Via via = emulatorUp() ? Via::Emulator : m_ble->isConnected() ? Via::Ble : Via::None;
    if (via == m_via)
        return;
    const bool wasConnected = isConnected();
    m_via = via;
    if (wasConnected)
        emit connectedChanged(false); // another player: the controller forgets what it told the old one
    if (isConnected())
        emit connectedChanged(true);
}

void DeviceLink::tryConnect()
{
    if (m_socket.state() == QAbstractSocket::UnconnectedState)
        m_socket.connectToHost(QHostAddress::LocalHost, kEmulatorPort);
}

void DeviceLink::send(const MediaSpin::Message &message)
{
    switch (m_via) {
    case Via::Emulator:
        qDebug().noquote() << "[device] ->" << MediaSpin::describe(message);
        m_socket.write(MediaSpin::frame(message));
        break;
    case Via::Ble:
        m_ble->send(message);
        break;
    case Via::None:
        break;
    }
}
