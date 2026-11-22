#pragma once
// ECDSA P-256 signature check for the release checksum list, with Windows' own BCrypt.

#include <QByteArray>
#include <QList>

namespace f1::releasesig {

// True if `signature` (64 bytes, IEEE P1363 r||s) is a valid ECDSA P-256 / SHA-256 signature of
// `data` by `publicKey` (64 bytes, X||Y).
bool verify(const QByteArray& data, const QByteArray& signature, const QByteArray& publicKey);

// The keys built into this app (core/release_key.h), decoded; malformed entries are skipped.
QList<QByteArray> builtInKeys();

// True if any of `keys` signed `data`.
bool verifyAny(const QByteArray& data, const QByteArray& signature, const QList<QByteArray>& keys);

}  // namespace f1::releasesig
