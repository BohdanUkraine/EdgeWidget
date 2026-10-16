// libsecret (glib) before Qt: its headers use the word "signals", which Qt defines as a macro.
#ifdef HAVE_LIBSECRET
#include <libsecret/secret.h>
#endif

#include "platform/SecretStore.h"

#include <QSettings>

// Secret Service (GNOME Keyring, KWallet) through libsecret. Without it (not built in, or no
// keyring running) the value goes to the settings file, readable only by this user.
namespace {
const QString kFallbackGroup = QStringLiteral("secrets/");

#ifdef HAVE_LIBSECRET
const SecretSchema *schema()
{
    static const SecretSchema s = {
        "com.edgewidget.EdgeWidget", SECRET_SCHEMA_NONE,
        { { "key", SECRET_SCHEMA_ATTRIBUTE_STRING }, { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING } },
    };
    return &s;
}
#endif
} // namespace

namespace SecretStore {

QString load(const QString &key)
{
#ifdef HAVE_LIBSECRET
    GError *error = nullptr;
    gchar *value = secret_password_lookup_sync(schema(), nullptr, &error, "key", key.toUtf8().constData(), nullptr);
    if (error)
        g_error_free(error);
    if (value) {
        const QString result = QString::fromUtf8(value);
        secret_password_free(value);
        return result;
    }
#endif
    return QSettings().value(kFallbackGroup + key).toString();
}

void save(const QString &key, const QString &value)
{
    QSettings settings;
    settings.remove(kFallbackGroup + key);
#ifdef HAVE_LIBSECRET
    GError *error = nullptr;
    bool stored = false;
    if (value.isEmpty())
        secret_password_clear_sync(schema(), nullptr, &error, "key", key.toUtf8().constData(), nullptr);
    else
        stored = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, "Edge Widget",
                                            value.toUtf8().constData(), nullptr, &error,
                                            "key", key.toUtf8().constData(), nullptr);
    if (error)
        g_error_free(error);
    if (stored || value.isEmpty())
        return;
#endif
    if (!value.isEmpty())
        settings.setValue(kFallbackGroup + key, value);
}

} // namespace SecretStore
