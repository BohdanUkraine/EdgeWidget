#include "device/BleLink.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QDebug>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>

#include <utility>

namespace {
const QString kDeviceName = QStringLiteral("MediaSpin");
const QBluetoothUuid kService(QStringLiteral("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
const QBluetoothUuid kRx(QStringLiteral("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")); // PC -> device (write)
const QBluetoothUuid kTx(QStringLiteral("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")); // device -> PC (notify)

constexpr int kScanMs = 5000;
constexpr int kRetryMs = 2000;       // after a lost connection / an empty scan
constexpr int kNoAdapterMs = 10000;  // Bluetooth off or missing: check now and then, quietly
} // namespace

BleLink::BleLink(QObject *parent)
    : QObject(parent)
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
{
    m_agent->setLowEnergyDiscoveryTimeout(kScanMs);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &BleLink::onDeviceFound);
    // The name comes in the scan response, often in a later update than the first sighting.
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
    scan();
}

BleLink::~BleLink()
{
    if (m_controller)
        m_controller->disconnectFromDevice();
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
    qDebug().noquote() << "[device] BLE seen:" << info.address().toString() << "name" << info.name()
                       << "uuids" << info.serviceUuids().size() << "config" << int(info.coreConfigurations());
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
                    if (auto message = MediaSpin::decode(value)) {
                        qDebug().noquote() << "[device] <-" << MediaSpin::describe(*message);
                        emit messageReceived(*message);
                    }
                });
        connect(m_service, &QLowEnergyService::descriptorWritten, this, [this](const QLowEnergyDescriptor &d, const QByteArray &) {
            if (m_ready || d.uuid() != QBluetoothUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration))
                return;
            m_ready = true;
            qDebug() << "[device] BLE: connected, MTU" << m_controller->mtu();
            emit connectedChanged(true);
        });
        connect(m_service, &QLowEnergyService::characteristicWritten, this, [this] {
            m_writing = false;
            writeNext();
        });
        connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError error) {
            qDebug() << "[device] BLE service error:" << error;
            if (error == QLowEnergyService::CharacteristicWriteError) { // lose this one, keep going
                m_writing = false;
                writeNext();
            } else if (!m_ready) {
                drop(kRetryMs);
            }
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
    m_queue.append(MediaSpin::encode(message)); // BLE keeps message boundaries: no length prefix
    writeNext();
}

void BleLink::writeNext()
{
    if (m_writing || m_queue.isEmpty() || !m_service)
        return;
    m_writing = true;
    m_service->writeCharacteristic(m_rx, m_queue.takeFirst(), QLowEnergyService::WriteWithResponse);
}

void BleLink::drop(int retryMs)
{
    const bool wasReady = std::exchange(m_ready, false);
    m_queue.clear();
    m_writing = false;
    m_rx = {};
    m_tx = {};
    if (m_service)
        m_service->disconnect(this);
    if (m_controller) {
        QLowEnergyController *controller = std::exchange(m_controller, nullptr);
        controller->disconnect(this); // no disconnected() back into drop()
        controller->disconnectFromDevice();
        controller->deleteLater(); // takes the service with it
    }
    if (wasReady)
        emit connectedChanged(false);
    m_retry.start(retryMs);
}
