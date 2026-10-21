#pragma once

#include "device/MediaSpinProtocol.h"

#include <QBluetoothDeviceInfo>
#include <QList>
#include <QLowEnergyCharacteristic>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;
class QLowEnergyService;

// BLE connection to the real MediaSpin: Nordic UART Service on the device named "MediaSpin",
// one message per write (RX) / notify (TX), see shared/PROTOCOL.md. Scans, connects,
// subscribes to TX and reconnects by itself, without pairing in the OS settings;
// "connected" means notifications are on.
//
// Delivery works like TCP: every command gets a number (seq), the device answers ACK with it once
// it has done the command, and the next one goes only after that. No ACK in kAckMs: the same
// command again (the device does a repeated seq only once), up to kMaxTries times.
class BleLink : public QObject
{
    Q_OBJECT

public:
    explicit BleLink(QObject *parent = nullptr);
    ~BleLink() override;

    bool isConnected() const { return m_ready; }
    void send(const MediaSpin::Message &message);

signals:
    void connectedChanged(bool connected);
    void messageReceived(const MediaSpin::Message &message);

private:
    void scan();
    void onDeviceFound(const QBluetoothDeviceInfo &info);
    void connectTo(const QBluetoothDeviceInfo &info);
    void onServiceState();
    void sendHead();         // the oldest unacknowledged command, if none is in flight
    void onAckTimeout();
    void drop(int retryMs); // forget the connection, try again after retryMs

    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QLowEnergyController *m_controller = nullptr;
    QPointer<QLowEnergyService> m_service;
    QLowEnergyCharacteristic m_rx;
    QLowEnergyCharacteristic m_tx;
    QTimer m_retry;
    bool m_ready = false;

    // One command in flight at a time, until its ACK; written with response.
    QList<MediaSpin::Message> m_queue;
    bool m_inFlight = false;
    int m_tries = 0;
    quint32 m_nextSeq = 1;
    QTimer m_ackTimer;
};
