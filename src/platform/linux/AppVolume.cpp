#include "platform/AppVolume.h"

// Not done on Linux (PipeWire / PulseAudio differ); the Web API handles the volume instead.
namespace AppVolume {

bool setSpotify(int)
{
    return false;
}

} // namespace AppVolume
