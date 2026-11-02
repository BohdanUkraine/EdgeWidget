#pragma once

// Volume through the OS.
namespace AppVolume {
// The whole computer: the default output device. False if it could not be changed.
bool setSystem(int percent);
// Spotify's own volume in the OS mixer (what the Windows volume mixer shows per program).
// Only a fallback for when the Web API is not signed in.
bool setSpotify(int percent); // false if not supported here or Spotify has no audio session
} // namespace AppVolume
