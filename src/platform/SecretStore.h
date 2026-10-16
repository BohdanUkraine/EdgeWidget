#pragma once

#include <QString>

// Secrets only this OS user can read back (the Spotify refresh token).
// Windows: DPAPI-encrypted in QSettings. macOS: the login Keychain.
namespace SecretStore {
QString load(const QString &key);
void save(const QString &key, const QString &value); // empty value removes it
} // namespace SecretStore
