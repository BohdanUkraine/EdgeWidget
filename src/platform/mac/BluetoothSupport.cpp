#include "platform/BluetoothSupport.h"

namespace BluetoothSupport {
// Every Mac since 2011 has Bluetooth LE; an off radio shows in the scan's own error.
State check() { return State::Unknown; }
}
