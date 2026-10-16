#pragma once

// The Spotify desktop app as a program: is it running, start it without a window.
namespace SpotifyApp {
bool isRunning();
bool launchQuietly(); // false if it could not be started at all
} // namespace SpotifyApp
