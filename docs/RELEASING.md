# Releasing

Installed copies update themselves from GitHub releases, but they only install a setup that is
listed in a `SHA256SUMS.txt` signed with the F1DockTimers release key. That key lives on the
maintainer's PC, encrypted with a password, never on GitHub: a hijacked account, token or workflow
can publish files, but not a signature the app accepts.

## Once: create the release key

In a terminal, from the repository folder:

```powershell
powershell -ExecutionPolicy Bypass -File tools\new-release-key.ps1
```

- Asks for a password twice (12 or more characters). It is typed into the script only; nothing
  stores or logs it.
- Writes the encrypted private key to `%USERPROFILE%\.release-keys\f1docktimers.key` (outside the
  repository) and refuses to overwrite an existing key file.
- Adds the public key to `src\core\release_key.h`. Commit that file; the next build carries it.
- **Back up the key file and keep the password** (a password manager is a good place for both). If
  either is lost, installed copies can no longer update themselves: you would make a new key, and
  everyone would install one release by hand.

## Every release

1. Set the version in `CMakeLists.txt` (`project(F1DockTimers VERSION x.y.z ...)`), commit, and
   publish a release `vx.y.z` on GitHub. CI builds the setup, the portable zip and
   `SHA256SUMS.txt` and attaches them.
2. Sign the checksum list:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\sign-release.ps1 -Version x.y.z -Upload
   ```

   It downloads the release's `SHA256SUMS.txt`, shows it (and whether it matches `dist\` if you
   built the same version here), asks you to type `yes`, asks for the key password and writes
   `dist\SHA256SUMS.txt.sig`. `-Upload` attaches it to the release with the GitHub CLI (`gh`);
   without it the script opens the release's edit page so you can upload it by hand.
3. From then on installed copies offer **Install and restart**. Before the `.sig` is there they
   show the update but only link to the release page.

Only sign a release you published yourself: the signature is the step where a malicious upload
would be caught. Releases published before the key existed stay as they are; the first signed
release is the next version after the key is in the app.

## What the app checks before running a setup

- The release is the repository's latest, not a draft or prerelease, and newer than the app.
- `SHA256SUMS.txt.sig` is a valid ECDSA P-256 signature (64 bytes, r||s, over SHA-256) of
  `SHA256SUMS.txt` by a key in `src\core\release_key.h`, checked with Windows' BCrypt.
- The signed list names `F1DockTimers-<version>-setup.exe` exactly once, with the same SHA-256
  that GitHub reports for the file.
- The downloaded setup has exactly that SHA-256 (and, when the app itself is code-signed, an
  Authenticode signature by the same publisher).
- Downloads use GitHub's hosts only (`github.com`, `objects.githubusercontent.com`,
  `release-assets.githubusercontent.com`), with every redirect checked and no fall back to HTTP.

## Changing the key

Run `tools\new-release-key.ps1 -KeyFile <new file>`: it adds the new public key next to the old one
in `src\core\release_key.h`. Publish a release signed with the old key that contains both, sign with
the new key from the next release on, and remove the old key later. A lost or leaked key cannot be
retired this way: remove it from the header, and users of older versions install the next release
by hand.

## Testing the scripts

`powershell -ExecutionPolicy Bypass -File tools\test-release-key.ps1` creates a throwaway key in a
temp folder and checks creation, refusal to overwrite, signing, wrong password and a changed list.
With `-WriteFixture` it refreshes `tests\fixtures\release_sig\`, which `tst_updater` uses to check
that the app accepts what the scripts sign.
