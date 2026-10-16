#pragma once

// Spotify's own volume in the OS mixer (what the Windows volume mixer shows per program).
// Only a fallback for when the Web API is not signed in.
namespace AppVolume {
bool setSpotify(int percent); // false if not supported here or Spotify has no audio session
} // namespace AppVolume
