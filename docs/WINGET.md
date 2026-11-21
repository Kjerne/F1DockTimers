# Publishing F1DockTimers on winget

After a release is published on GitHub (with its `-setup.exe`):

1. Make the manifests (reads the file's SHA-256 from GitHub, nothing is downloaded):

   ```powershell
   python tools\make_winget.py 1.0.6
   ```

   They land in `manifests\k\Kjerne\F1DockTimers\1.0.6\`.
2. Optional check, if you have winget: `winget validate manifests\k\Kjerne\F1DockTimers\1.0.6`
3. Fork <https://github.com/microsoft/winget-pkgs>, add the folder at the same path
   (`manifests/k/Kjerne/F1DockTimers/1.0.6/`) and open a pull request. Their bots check it;
   a moderator merges it, usually within a few days.

After that, anyone can install it with:

```
winget install Kjerne.F1DockTimers
```

For later versions, repeat with the new version number (the first pull request is the slow one).
The `manifests/` folder in this repository is only a working copy and is not needed in git.
