#include "platform/AppVolume.h"

// macOS has no per-app volume API; the Web API handles the volume instead.
namespace AppVolume {

bool setSpotify(int)
{
    return false;
}

} // namespace AppVolume
