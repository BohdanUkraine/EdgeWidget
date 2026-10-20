#pragma once

#include "device/MediaSpinProtocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class BleLink;

// Connection to the record player: BLE to the real MediaSpin (BleLink), or TCP to the
// emulator on this PC (emulator/mediaspin_emulator.py) when it is running. Same messages
// either way; while the emulator is up it wins, so it can be tried next to a real device.
// Both sides reconnect by themselves.
class DeviceLink : public QObject
{
    Q_OBJECT

public:
    explicit DeviceLink(QObject *parent = nullptr);

    bool isConnected() const;
    void send(const MediaSpin::Message &message);

signals:
    void connectedChanged(bool connected);
    void messageReceived(const MediaSpin::Message &message);

private:
    enum class Via { None, Ble, Emulator };

    void tryConnect();
    void update(); // pick the transport; a switch counts as a reconnect
    bool emulatorUp() const { return m_socket.state() == QAbstractSocket::ConnectedState; }

    BleLink *m_ble;
    QTcpSocket m_socket;
    QTimer m_retry;
    MediaSpin::FrameReader m_reader;
    Via m_via = Via::None;
};
