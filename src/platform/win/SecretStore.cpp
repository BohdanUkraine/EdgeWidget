#include "platform/SecretStore.h"

#include <QSettings>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>

namespace {
// Windows DPAPI: only this Windows user can decrypt it.
QByteArray dpapi(const QByteArray &data, bool encrypt)
{
    DATA_BLOB in{ DWORD(data.size()), reinterpret_cast<BYTE *>(const_cast<char *>(data.data())) };
    DATA_BLOB out{};
    const BOOL ok = encrypt
        ? CryptProtectData(&in, L"EdgeWidget", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
        : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok)
        return {};
    QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result;
}
} // namespace

namespace SecretStore {

QString load(const QString &key)
{
    const QByteArray sealed = QByteArray::fromBase64(QSettings().value(key).toByteArray());
    return sealed.isEmpty() ? QString() : QString::fromUtf8(dpapi(sealed, false));
}

void save(const QString &key, const QString &value)
{
    QSettings s;
    if (value.isEmpty())
        s.remove(key);
    else
        s.setValue(key, dpapi(value.toUtf8(), true).toBase64());
}

} // namespace SecretStore
