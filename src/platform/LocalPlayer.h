#pragma once

#include "app/MediaState.h"

#include <functional>
#include <memory>

class LocalPlayer
{
public:
    static std::unique_ptr<LocalPlayer> create(std::function<void()> notify);
    virtual ~LocalPlayer() = default;

    virtual MediaState poll() = 0;
    virtual void reset() = 0;

    virtual void togglePlayPause() = 0;
    virtual void play() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void setShuffle(bool on) = 0;
    virtual void setRepeat(int mode) = 0;
    virtual void seek(qint64 positionMs) = 0;
};
