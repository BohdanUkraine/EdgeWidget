#include "platform/BluetoothSupport.h"

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.h>

using winrt::Windows::Devices::Bluetooth::BluetoothAdapter;
using winrt::Windows::Devices::Radios::RadioState;

namespace BluetoothSupport {
State check()
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error &) {
        // already initialised on this thread: fine
    }
    State state = State::Unknown;
    try {
        const BluetoothAdapter adapter = BluetoothAdapter::GetDefaultAsync().get();
        if (!adapter)
            state = State::NoAdapter;
        // some adapters (CSR / Barrot clones) do classic Bluetooth only: Windows keeps LE off on them
        else if (!adapter.IsLowEnergySupported() || !adapter.IsCentralRoleSupported())
            state = State::NoLowEnergy;
        else if (const auto radio = adapter.GetRadioAsync().get(); radio && radio.State() != RadioState::On)
            state = State::Off;
        else
            state = State::Ready;
    } catch (const winrt::hresult_error &) {
    }
    return state;
}
}
