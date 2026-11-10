#include "platform/BluetoothSupport.h"

namespace BluetoothSupport {
// BlueZ tells nothing as plain as Windows does; the scan's own error has to do.
State check() { return State::Unknown; }
}
