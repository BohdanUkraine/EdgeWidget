#pragma once

#include "app/MediaState.h"

#include <functional>
#include <memory>

// The Spotify desktop app on this computer, through what the OS offers:
//  - Windows: media sessions (GSMTC), event driven  -> platform/win/WinLocalPlayer.cpp
//  - macOS:   Spotify's AppleScript interface, polled -> platform/mac/MacLocalPlayer.cpp
// Created, used and destroyed on MediaController's worker thread; calls may block.
// Any call may throw when the player goes away: the caller then calls reset().
class LocalPlayer
{
public:
    // notify: "something changed, poll() again"; may be called from any thread.
    static std::unique_ptr<LocalPlayer> create(std::function<void()> notify);
    virtual ~LocalPlayer() = default;

    virtual MediaState poll() = 0; // available == false: Spotify does not run here
    virtual void reset() = 0;      // forget cached handles, reconnect on the next poll()

    virtual void togglePlayPause() = 0;
    virtual void play() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void setShuffle(bool on) = 0;
    virtual void setRepeat(int mode) = 0; // 0 = off, 1 = track, 2 = list
    virtual void seek(qint64 positionMs) = 0;
};
