#include "wintrust.h"

#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first; these need its types.
#include <appmodel.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace f1::wintrust {

update::Signature authenticode(const QString& path) {
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;   // as Get-AuthenticodeSignature: no network lookups
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG rc = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);

    update::Signature out;
    switch (static_cast<DWORD>(rc)) {
    case ERROR_SUCCESS: out.status = QStringLiteral("Valid"); break;
    case static_cast<DWORD>(TRUST_E_NOSIGNATURE):
    case static_cast<DWORD>(TRUST_E_SUBJECT_FORM_UNKNOWN):
    case static_cast<DWORD>(TRUST_E_PROVIDER_UNKNOWN): out.status = QStringLiteral("NotSigned"); break;
    case static_cast<DWORD>(TRUST_E_BAD_DIGEST): out.status = QStringLiteral("HashMismatch"); break;
    case static_cast<DWORD>(CERT_E_UNTRUSTEDROOT):
    case static_cast<DWORD>(CERT_E_CHAINING):
    case static_cast<DWORD>(TRUST_E_EXPLICIT_DISTRUST):
    case static_cast<DWORD>(CERT_E_EXPIRED): out.status = QStringLiteral("NotTrusted"); break;
    default: out.status = QStringLiteral("UnknownError"); break;
    }
    if (out.status == u"Valid") {
        // The signer's subject in .NET's X509Certificate.Subject form ("CN=..., O=..., C=...").
        if (CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData))
            if (CRYPT_PROVIDER_SGNR* sg = WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0))
                if (sg->csCertChain > 0 && sg->pasCertChain && sg->pasCertChain[0].pCert) {
                    const PCERT_INFO ci = sg->pasCertChain[0].pCert->pCertInfo;
                    const DWORD flags = CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG;
                    const DWORD n = CertNameToStrW(X509_ASN_ENCODING, &ci->Subject, flags, nullptr, 0);
                    if (n > 1) {
                        std::wstring buf(n, L'\0');
                        CertNameToStrW(X509_ASN_ENCODING, &ci->Subject, flags, buf.data(), n);
                        out.subject = QString::fromWCharArray(buf.c_str());
                    }
                }
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    return out;
}

bool isPackaged() {
    UINT32 n = 0;
    const LONG rc = GetCurrentPackageFamilyName(&n, nullptr);
    return rc != APPMODEL_ERROR_NO_PACKAGE && n > 0;
}

}  // namespace f1::wintrust
