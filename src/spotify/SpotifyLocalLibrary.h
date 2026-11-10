#pragma once

#include <QStringList>

// What the Web API does not give: the pins and folders from "Your Library", read from the
// desktop app's own LevelDB cache. The format is undocumented, so anything unexpected just
// yields nothing.
namespace SpotifyLocalLibrary {
struct Data
{
    bool found = false;  // the cache exists for this user
    QStringList pinned;  // context URIs, oldest pin first; Liked Songs is "spotify:collection"
    // The playlist list in its own order, folders as brackets around their contents:
    // "spotify:start-group:<id>:<url-encoded name>" ... "spotify:end-group:<id>".
    QStringList rootlist;
};

Data read(const QString &userId); // blocking: reads files, call it off the UI thread
}
