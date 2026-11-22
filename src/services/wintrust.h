#pragma once
// Windows' own answers about files and this copy: Authenticode signature (instead of the Python
// app's Get-AuthenticodeSignature through PowerShell) and whether this is the Microsoft Store
// (MSIX) copy (port of services/packaging.py's is_packaged).

#include "../core/update_rules.h"

namespace f1::wintrust {

// Status names as PowerShell reports them: Valid, NotSigned, HashMismatch, NotTrusted,
// UnknownError. The subject is the signer certificate's subject, only for a valid signature.
update::Signature authenticode(const QString& path);

// True when running from an MSIX package (the Store updates that copy itself).
bool isPackaged();

}  // namespace f1::wintrust
