#include "device/BleLink.h"

#include "platform/BluetoothSupport.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QCoreApplication>
#include <QThread>
#include <QBluetoothUuid>
#include <QDebug>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>

#include <utility>

namespace {
const QString kDeviceName = QStringLiteral("MediaSpin");
const QBluetoothUuid kService(QStringLiteral("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
const QBluetoothUuid kRx(QStringLiteral("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"));
const QBluetoothUuid kTx(QStringLiteral("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"));

constexpr int kScanMs = 5000;
constexpr int kRetryMs = 2000;
constexpr int kNoAdapterMs = 10000;
constexpr int kAckMs = 600;
constexpr int kMaxTries = 4;
constexpr int kConnectMs = 10000; // connected and notifying by then, or start over
}

BleLink::BleLink(QObject *parent)
    : QObject(parent)
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
{
    m_agent->setLowEnergyDiscoveryTimeout(kScanMs);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &BleLink::onDeviceFound);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
            [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { onDeviceFound(info); });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this] {
        qDebug() << "[device] BLE scan finished:" << m_agent->discoveredDevices().size() << "devices";
        if (!m_controller)
            m_retry.start(kRetryMs);
    });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [this](QBluetoothDeviceDiscoveryAgent::Error error) {
        qDebug() << "[device] BLE scan error:" << error << m_agent->errorString();
        if (!m_controller)
            m_retry.start(kNoAdapterMs);
    });

    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &BleLink::scan);
    m_connectTimeout.setSingleShot(true);
    m_connectTimeout.setInterval(kConnectMs);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] {
        qDebug() << "[device] BLE: not ready after" << kConnectMs << "ms - starting over";
        drop(kRetryMs);
    });
    m_ackTimer.setSingleShot(true);
    m_ackTimer.setInterval(kAckMs);
    connect(&m_ackTimer, &QTimer::timeout, this, &BleLink::onAckTimeout);
    QTimer::singleShot(0, this, &BleLink::checkAdapter); // once the owner listens; it starts the scan
}

BleLink::~BleLink()
{
    if (m_controller)
        m_controller->disconnectFromDevice();
}

// Once, at start: said once, and without Bluetooth LE there is no scanning at all, the widget
// is then just a player. A radio that is off may come on, so the scan runs anyway.
void BleLink::checkAdapter()
{
    QPointer<BleLink> self(this);
    QThread *thread = QThread::create([self] {
        const BluetoothSupport::State state = BluetoothSupport::check();
        QMetaObject::invokeMethod(qApp, [self, state] {
            if (!self)
                return;
            qDebug() << "[device] Bluetooth adapter:" << int(state);
            switch (state) {
            case BluetoothSupport::State::NoAdapter:
                emit self->problem(tr("На цьому ПК немає Bluetooth — програвач не підключиться"));
                return;
            case BluetoothSupport::State::NoLowEnergy:
                emit self->problem(tr("Bluetooth цього ПК не підтримує Bluetooth LE — потрібен адаптер Bluetooth 4.0+"));
                return;
            case BluetoothSupport::State::Off:
                emit self->problem(tr("Bluetooth вимкнено — увімкни його, щоб підключити програвач"));
                break;
            case BluetoothSupport::State::Ready:
            case BluetoothSupport::State::Unknown:
                break;
            }
            self->scan();
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

void BleLink::scan()
{
    if (m_controller || m_agent->isActive())
        return;
    qDebug() << "[device] BLE scan started";
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleLink::onDeviceFound(const QBluetoothDeviceInfo &info)
{
    if (m_controller || !(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration))
        return;
    if (info.name() != kDeviceName && !info.serviceUuids().contains(kService))
        return;
    m_agent->stop();
    connectTo(info);
}

void BleLink::connectTo(const QBluetoothDeviceInfo &info)
{
    qDebug().noquote() << "[device] BLE: connecting to" << info.name() << info.address().toString();
    m_controller = QLowEnergyController::createCentral(info, this);
    m_connectTimeout.start();

    connect(m_controller, &QLowEnergyController::connected, m_controller, &QLowEnergyController::discoverServices);
    connect(m_controller, &QLowEnergyController::disconnected, this, [this] { drop(kRetryMs); });
    connect(m_controller, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error error) {
        qDebug() << "[device] BLE error:" << error;
        drop(kRetryMs);
    });
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, [this] {
        m_service = m_controller->createServiceObject(kService, m_controller);
        if (!m_service) {
            qDebug() << "[device] BLE: no MediaSpin service on the device";
            drop(kRetryMs);
            return;
        }
        connect(m_service, &QLowEnergyService::stateChanged, this, &BleLink::onServiceState);
        connect(m_service, &QLowEnergyService::characteristicChanged, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &value) {
                    if (c.uuid() != kTx)
                        return;
                    auto message = MediaSpin::decode(value);
                    if (!message) {
                        qDebug() << "[device] BLE: undecodable message" << value.toHex(' ');
                        return;
                    }
                    if (message->command == MediaSpin::Command::Ack) {
                        if (m_inFlight && !m_queue.isEmpty() && message->seq == m_queue.first().seq) {
                            m_ackTimer.stop();
                            m_queue.removeFirst();
                            m_inFlight = false;
                            m_tries = 0;
                            sendHead();
                        }
                        return;
                    }
                    qDebug().noquote() << "[device] <-" << MediaSpin::describe(*message);
                    emit messageReceived(*message);
                });
        connect(m_service, &QLowEnergyService::descriptorWritten, this, [this](const QLowEnergyDescriptor &d, const QByteArray &) {
            if (m_ready || d.uuid() != QBluetoothUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration))
                return;
            m_ready = true;
            m_connectTimeout.stop();
            m_nextSeq = 1;
            qDebug() << "[device] BLE: connected, MTU" << m_controller->mtu();
            emit connectedChanged(true);
        });
        connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError error) {
            qDebug() << "[device] BLE service error:" << error;
            if (error != QLowEnergyService::CharacteristicWriteError && !m_ready)
                drop(kRetryMs);
        });
        m_service->discoverDetails();
    });

    m_controller->connectToDevice();
}

void BleLink::onServiceState()
{
    if (m_service->state() != QLowEnergyService::RemoteServiceDiscovered)
        return;
    m_rx = m_service->characteristic(kRx);
    m_tx = m_service->characteristic(kTx);
    const QLowEnergyDescriptor cccd = m_tx.clientCharacteristicConfiguration();
    if (!m_rx.isValid() || !m_tx.isValid() || !cccd.isValid()) {
        qDebug() << "[device] BLE: the service has no RX / TX";
        drop(kRetryMs);
        return;
    }
    m_service->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDEnableNotification);
}

void BleLink::send(const MediaSpin::Message &message)
{
    if (!m_ready)
        return;
    qDebug().noquote() << "[device] ->" << MediaSpin::describe(message);
    MediaSpin::Message numbered = message;
    numbered.seq = m_nextSeq++;
    if (!m_nextSeq)
        m_nextSeq = 1;
    m_queue.append(numbered);
    sendHead();
}

void BleLink::sendHead()
{
    if (m_inFlight || m_queue.isEmpty() || !m_service)
        return;
    m_inFlight = true;
    m_tries++;
    m_service->writeCharacteristic(m_rx, MediaSpin::encode(m_queue.first()), QLowEnergyService::WriteWithResponse);
    m_ackTimer.start();
}

void BleLink::onAckTimeout()
{
    if (m_queue.isEmpty())
        return;
    const MediaSpin::Message &head = m_queue.first();
    m_inFlight = false;
    if (m_tries < kMaxTries) {
        qDebug().noquote() << "[device] BLE: no ACK for" << MediaSpin::describe(head) << "seq" << head.seq
                           << "- sending again";
    } else {
        qDebug().noquote() << "[device] BLE: gave up on" << MediaSpin::describe(head) << "seq" << head.seq;
        m_queue.removeFirst();
        m_tries = 0;
    }
    sendHead();
}

void BleLink::drop(int retryMs)
{
    const bool wasReady = std::exchange(m_ready, false);
    m_queue.clear();
    m_inFlight = false;
    m_tries = 0;
    m_ackTimer.stop();
    m_connectTimeout.stop();
    m_rx = {};
    m_tx = {};
    if (m_service)
        m_service->disconnect(this);
    if (m_controller) {
        QLowEnergyController *controller = std::exchange(m_controller, nullptr);
        controller->disconnect(this);
        controller->disconnectFromDevice();
        controller->deleteLater();
    }
    if (wasReady)
        emit connectedChanged(false);
    m_retry.start(retryMs);
}
