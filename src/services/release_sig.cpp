#include "release_sig.h"

#include "../core/release_key.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first; bcrypt.h needs its types.
#include <bcrypt.h>

namespace f1::releasesig {

bool verify(const QByteArray& data, const QByteArray& signature, const QByteArray& publicKey) {
    if (signature.size() != 64 || publicKey.size() != 64) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) return false;
    // BCRYPT_ECCPUBLIC_BLOB: the header, then X and Y.
    std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), publicKey.constData(), 64);
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;
    if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0) == 0) {
        QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        QByteArray sig = signature;
        ok = BCryptVerifySignature(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()),
                                   reinterpret_cast<PUCHAR>(sig.data()), static_cast<ULONG>(sig.size()), 0) == 0;
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

QList<QByteArray> builtInKeys() {
    static const QRegularExpression hex128(QStringLiteral("^[0-9a-fA-F]{128}$"));
    QList<QByteArray> keys;
    for (const char* const* k = update::RELEASE_PUBLIC_KEYS; *k; ++k)
        if (hex128.match(QLatin1StringView(*k)).hasMatch()) keys << QByteArray::fromHex(*k);
    return keys;
}

bool verifyAny(const QByteArray& data, const QByteArray& signature, const QList<QByteArray>& keys) {
    for (const QByteArray& k : keys)
        if (verify(data, signature, k)) return true;
    return false;
}

}  // namespace f1::releasesig
