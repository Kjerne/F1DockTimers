"""The pull request tripwire (tools/pr_safety.py) flags what it should and stays quiet otherwise."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("pr_safety", Path(__file__).parents[1] / "tools" / "pr_safety.py")
pr_safety = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pr_safety)


def diff(path, *added):
    body = "\n".join("+" + a for a in added)
    return f"diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n@@ -1,0 +1,{len(added)} @@\n{body}\n"


def whys(d):
    return [f[2] for f in pr_safety.scan(d)]


def test_clean_change_passes():
    assert whys(diff("f1win/ui/panel.py", "label = Text('Hello')", "url = 'https://api.jolpi.ca/ergast/f1'")) == []


def test_red_flags():
    assert "runs other programs" in whys(diff("f1win/x.py", "subprocess.run(['cmd'])"))
    assert "new web address (evil.example)" in whys(diff("f1win/x.py", "URL = 'https://evil.example/a'"))
    assert "registry / startup changes" in whys(diff("f1win/x.py", "winreg.SetValueEx(k, 'X', 0, 1, v)"))
    assert "credentials near logs" in whys(diff("f1win/x.py", "log.info('login %s', password)"))
    assert "plaintext secret" in whys(diff("f1win/x.py", "token = 'abcdef123456'"))
    assert "reads input, clipboard or screen" in whys(diff("f1win/x.py", "img = screen.grabWindow(0)"))
    assert "dynamic code" in whys(diff("f1win/x.py", "eval(user_text)"))
    assert "deletes or moves files" in whys(diff("f1win/x.py", "shutil.rmtree(folder)"))


def test_sensitive_paths_and_binaries():
    assert "sensitive file" in whys(diff(".github/workflows/windows-build.yml", "run: echo hi"))
    assert "binary file" in whys("diff --git a/x.exe b/x.exe\n+++ b/x.exe\nBinary files /dev/null and b/x.exe differ\n")


def test_line_numbers():
    d = diff("f1win/x.py", "a = 1", "b = 2", "subprocess.call('x')")
    assert pr_safety.scan(d)[0][1] == 3
