#include "platform/SecretStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <QByteArray>

namespace {
// One generic password per key in the login Keychain, under the service "EdgeWidget".
CFMutableDictionaryRef query(const QString &key)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, CFSTR("EdgeWidget"));
    CFStringRef account = key.toCFString();
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(account);
    return q;
}
} // namespace

namespace SecretStore {

QString load(const QString &key)
{
    CFMutableDictionaryRef q = query(key);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef data = nullptr;
    const OSStatus status = SecItemCopyMatching(q, &data);
    CFRelease(q);
    if (status != errSecSuccess || !data)
        return {};
    const QString value = QString::fromUtf8(QByteArray::fromCFData(static_cast<CFDataRef>(data)));
    CFRelease(data);
    return value;
}

void save(const QString &key, const QString &value)
{
    CFMutableDictionaryRef q = query(key);
    SecItemDelete(q); // replace, not update: simpler and the same result
    if (!value.isEmpty()) {
        CFDataRef data = value.toUtf8().toCFData();
        CFDictionarySetValue(q, kSecValueData, data);
        CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        SecItemAdd(q, nullptr);
        CFRelease(data);
    }
    CFRelease(q);
}

} // namespace SecretStore
