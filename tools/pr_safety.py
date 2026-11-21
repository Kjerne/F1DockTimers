"""Tripwire for pull requests: lists added lines a reviewer must read carefully before merging.

    usage: python tools/pr_safety.py <diff-file>        (exit code 1 when anything is flagged)

It only reads the diff as text and never runs code from the pull request. A clean result is not a
guarantee; it just points at the lines that matter most. Findings go to the GitHub step summary
(or stdout when run locally).
"""
from __future__ import annotations

import os
import re
import sys

# Hosts the app is meant to talk to (see PRIVACY.md). A new one must be added there too.
ALLOWED_HOSTS = {
    "api.jolpi.ca", "api.openf1.org", "livetiming.formula1.com", "api.github.com", "github.com",
    "objects.githubusercontent.com", "release-assets.githubusercontent.com", "en.wikipedia.org",
    # documentation / licence links that only appear in comments and docs
    "about.signpath.io", "signpath.org", "www.python.org", "doc.qt.io", "www.qt.io",
}
# Paths that control how the app is built, checked and released.
SENSITIVE = re.compile(r"^(\.github/|build/|tools/|pyproject\.toml$|requirements[^/]*\.txt$)")
CODE = re.compile(r"\.(py|pyw|ps1|psm1|bat|cmd|sh|iss|spec|js|ts)$")

RULES = [
    ("runs other programs", re.compile(
        r"\b(subprocess|os\.system|os\.popen|os\.startfile|ShellExecute|CreateProcess|QProcess|WinExec)\b")),
    ("dynamic code", re.compile(r"\b(eval|exec|compile|__import__|importlib\.import_module|marshal\.loads)\s*\(")),
    ("unsafe deserialisation", re.compile(r"\b(pickle|shelve|dill)\.(load|loads)\b|yaml\.load\s*\((?!.*SafeLoader)")),
    ("memory / process tricks", re.compile(
        r"\b(WriteProcessMemory|ReadProcessMemory|VirtualAlloc|VirtualProtect|CreateRemoteThread|OpenProcess|"
        r"LoadLibrary|GetProcAddress)\b")),
    ("reads input, clipboard or screen", re.compile(
        r"\b(SetWindowsHookEx|GetAsyncKeyState|GetKeyState|RegisterRawInputDevices|GetClipboardData|"
        r"OpenClipboard|QClipboard|clipboard\(\)|grabWindow|BitBlt|PrintWindow|ImageGrab|pynput|keyboard\.)")),
    ("registry / startup changes", re.compile(
        r"\b(winreg\.(SetValue|SetValueEx|CreateKey|CreateKeyEx|DeleteKey|DeleteValue)|RegSetValue|"
        r"RegSetKeyValue|RegCreateKey|RegDelete|schtasks)\b|CurrentVersion\\\\Run|\\\\Startup")),
    ("deletes or moves files", re.compile(
        r"\b(shutil\.(rmtree|move)|os\.(remove|unlink|rmdir|removedirs|rename|replace)|\.unlink\(|"
        r"\.rmdir\(|DeleteFile|MoveFile|SHFileOperation)\b")),
    ("credentials near logs", re.compile(
        r"\b(log(ger)?|logging)\.\w+\(.*\b(password|passwd|token|secret|authorization)\b", re.I)),
    ("plaintext secret", re.compile(
        r"\b(password|passwd|api_key|apikey|secret|token)\s*[:=]\s*[\"'][^\"']{6,}[\"']", re.I)),
    ("long encoded blob (possible hidden payload)", re.compile(
        r"[\"'][A-Za-z0-9+/=]{160,}[\"']|(0x[0-9A-Fa-f]{2},\s*){48,}|b64decode|a85decode|zlib\.decompress")),
]
URL = re.compile(r"https?://([A-Za-z0-9.-]+)")


def scan(diff: str) -> list[tuple[str, int, str, str]]:
    findings: list[tuple[str, int, str, str]] = []
    path, line = "", 0
    for raw in diff.splitlines():
        if raw.startswith("+++ "):
            path = raw[4:].removeprefix("b/")
            if path != "/dev/null" and SENSITIVE.match(path):
                findings.append((path, 0, "sensitive file", "changes the build, release or check pipeline"))
            continue
        if raw.startswith("Binary files ") or raw.startswith("GIT binary patch"):
            findings.append((path or "?", 0, "binary file", raw[:120]))
            continue
        if raw.startswith("@@"):
            m = re.search(r"\+(\d+)", raw)
            line = int(m.group(1)) - 1 if m else 0
            continue
        if raw.startswith("-") or raw.startswith("diff --git") or raw.startswith("index "):
            continue
        if raw.startswith("+"):
            line += 1
            text = raw[1:]
            for host in URL.findall(text):
                if host.lower() not in ALLOWED_HOSTS:
                    findings.append((path, line, f"new web address ({host})", text.strip()[:160]))
            if not CODE.search(path):
                continue
            for why, rx in RULES:
                if rx.search(text):
                    findings.append((path, line, why, text.strip()[:160]))
        elif not raw.startswith("\\"):
            line += 1                      # context line
    return findings


def main() -> int:
    diff = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    findings = scan(diff)
    out = []
    out.append("## PR safety check\n")
    if not findings:
        out.append("No red flags found. Still read the diff before merging: this is a tripwire, not a guarantee.")
    else:
        out.append("These lines need a careful look. Some may be fine, but the pull request should explain each one.\n")
        out.append("| File | Line | Why | Code |")
        out.append("|---|---|---|---|")
        for f, ln, why, text in findings:
            cell = text.replace("|", "\\|").replace("`", "")
            out.append(f"| `{f}` | {ln or ''} | {why} | `{cell}` |")
            if os.environ.get("GITHUB_ACTIONS"):
                print(f"::warning file={f},line={ln}::{why}", file=sys.stderr)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write("\n".join(out) + "\n")
    else:
        print("\n".join(out))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
