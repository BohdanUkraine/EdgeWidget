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
    void sendHead();
    void onAckTimeout();
    void drop(int retryMs);

    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QLowEnergyController *m_controller = nullptr;
    QPointer<QLowEnergyService> m_service;
    QLowEnergyCharacteristic m_rx;
    QLowEnergyCharacteristic m_tx;
    QTimer m_retry;
    bool m_ready = false;

    QList<MediaSpin::Message> m_queue;
    bool m_inFlight = false;
    int m_tries = 0;
    quint32 m_nextSeq = 1;
    QTimer m_ackTimer;
};
