#pragma once

#include "device/MediaSpinProtocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

// Connection to the record player. For now TCP to the emulator on this PC
// (emulator/mediaspin_emulator.py); the real device will use BLE with the same messages.
// Reconnects by itself.
class DeviceLink : public QObject
{
    Q_OBJECT

public:
    explicit DeviceLink(QObject *parent = nullptr);

    bool isConnected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }
    void send(const MediaSpin::Message &message);

signals:
    void connectedChanged(bool connected);
    void messageReceived(const MediaSpin::Message &message);

private:
    void tryConnect();

    QTcpSocket m_socket;
    QTimer m_retry;
    MediaSpin::FrameReader m_reader;
};
