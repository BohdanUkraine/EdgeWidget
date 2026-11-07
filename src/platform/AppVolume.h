#pragma once

// Percent 0..100; -1: unknown or not supported here.
namespace AppVolume {
bool setSystem(int percent); // also unmutes when percent > 0
// Like the keyboard's volume keys: Windows shows its own volume popup.
bool stepSystem(int delta);
bool toggleSystemMute();
bool setSpotify(int percent); // Spotify in the OS mixer (Windows only)
int spotifyLevel();
}
