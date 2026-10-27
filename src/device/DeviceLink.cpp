#include "device/DeviceLink.h"

#include "device/BleLink.h"

DeviceLink::DeviceLink(QObject *parent)
    : QObject(parent)
    , m_ble(new BleLink(this))
{
    connect(m_ble, &BleLink::connectedChanged, this, &DeviceLink::connectedChanged);
    connect(m_ble, &BleLink::messageReceived, this, &DeviceLink::messageReceived);
}

bool DeviceLink::isConnected() const
{
    return m_ble->isConnected();
}

void DeviceLink::send(const MediaSpin::Message &message)
{
    m_ble->send(message);
}
