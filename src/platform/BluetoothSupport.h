#pragma once

// Whether this computer can reach the player over Bluetooth LE at all.
namespace BluetoothSupport {
enum class State { Ready, NoAdapter, NoLowEnergy, Off, Unknown };

State check(); // blocking: may wait on the OS, call it off the UI thread
}
