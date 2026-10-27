#pragma once

#include "device/MediaSpinProtocol.h"

#include <QObject>

class BleLink;

// Connection to the real MediaSpin record player: BLE only (BleLink), which scans, connects
// and reconnects by itself. Thin forwarding wrapper, kept so DeviceController doesn't depend
// on BleLink directly.
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
    BleLink *m_ble;
};
