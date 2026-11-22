#pragma once
// Public half of the F1DockTimers release key (ECDSA P-256), written by tools/new-release-key.ps1.
// The updater installs a release only if its SHA256SUMS.txt carries a valid signature by one of
// these keys; the private key never leaves the maintainer's PC. Each entry is 128 hex digits: the
// curve point's X then Y. More than one key is allowed while changing keys (docs/RELEASING.md).
// With no key here the app still shows new releases but never installs them itself.

namespace f1::update {

inline constexpr const char* RELEASE_PUBLIC_KEYS[] = {
    // keys:begin
    "2abee903b39da4601ea516594bda15ebaa18ec5e5fd2bd9703e769b2c141c0558407a18f258c3f97af93822608dc7808b26493e229d16a58126cd2a1eba97955",  // created 2026-11-22
    // keys:end
    nullptr,
};

}  // namespace f1::update
