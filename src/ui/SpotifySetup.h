#pragma once

class QWidget;
class SpotifyClient;

// Asks for the user's own Spotify Client ID (with setup steps) and starts the login.
// With a parent (the panel) the panel stays open while the dialog is shown.
void runSpotifySetup(SpotifyClient *spotify, QWidget *parent = nullptr);
