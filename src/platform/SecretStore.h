#pragma once

#include <QString>

namespace SecretStore {
QString load(const QString &key);
void save(const QString &key, const QString &value);
}
