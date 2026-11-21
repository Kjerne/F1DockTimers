from pathlib import Path

from f1win.services import updater as up

SHA = "a" * 64


def release(tag="v1.0.3", assets=None, **kw):
    return {"tag_name": tag, "name": f"F1DockTimers {tag}", "body": "Fixes", "draft": False, "prerelease": False,
            "html_url": "https://github.com/Kjerne/F1DockTimers/releases/tag/" + tag,
            "assets": assets if assets is not None else [
                {"name": "F1DockTimers-1.0.3-setup.exe", "size": 40_000_000, "digest": "sha256:" + SHA,
                 "browser_download_url": "https://github.com/Kjerne/F1DockTimers/releases/download/v1.0.3/F1DockTimers-1.0.3-setup.exe"},
                {"name": "F1DockTimers-1.0.3-portable.exe", "size": 44_000_000, "digest": "sha256:" + "b" * 64,
                 "browser_download_url": "https://github.com/Kjerne/F1DockTimers/releases/download/v1.0.3/F1DockTimers-1.0.3-portable.exe"},
            ], **kw}


def test_versions():
    assert up.parse_version("v1.0.3") == (1, 0, 3)
    assert up.parse_version("1.10.0") == (1, 10, 0)
    assert up.parse_version("v1.0.3-beta") is None and up.parse_version("") is None
    assert up.is_newer("v1.0.10", "1.0.9")
    assert not up.is_newer("v1.0.2", "1.0.2")
    assert not up.is_newer("v1.0.1", "1.0.2")
    assert not up.is_newer("garbage", "1.0.2")


def test_release_parsing_and_assets():
    rel = up.parse_release(release())
    assert rel["version"] == "1.0.3"
    setup = up.pick_asset(rel["raw"], "installed")
    port = up.pick_asset(rel["raw"], "portable")
    assert setup["name"].endswith("-setup.exe") and setup["sha256"] == SHA
    assert port["name"].endswith("-portable.exe")
    assert up.parse_release(release(prerelease=True)) is None
    assert up.parse_release(release(draft=True)) is None
    assert up.parse_release(release(tag="nightly")) is None
    assert up.parse_release({"html_url": "x"}) is None


def test_assets_without_checksum_or_from_elsewhere_are_refused():
    no_digest = release(assets=[{"name": "F1DockTimers-1.0.3-setup.exe", "size": 1,
                                 "browser_download_url": "https://github.com/x/y/z.exe"}])
    assert up.pick_asset(no_digest, "installed") is None
    other_host = release(assets=[{"name": "F1DockTimers-1.0.3-setup.exe", "size": 1, "digest": "sha256:" + SHA,
                                  "browser_download_url": "https://evil.example/F1DockTimers-1.0.3-setup.exe"}])
    assert up.pick_asset(other_host, "installed") is None
    bad_digest = release(assets=[{"name": "F1DockTimers-1.0.3-setup.exe", "size": 1, "digest": "md5:abc",
                                  "browser_download_url": "https://github.com/x"}])
    assert up.pick_asset(bad_digest, "installed") is None


def test_signature_rules():
    signed = ("Valid", "CN=Example Publisher")
    unsigned = ("NotSigned", "")
    assert up.signature_ok(unsigned, unsigned)[0]                 # today: unsigned -> unsigned ok (checksum still checked)
    assert up.signature_ok(unsigned, signed)[0]                   # a later signed release
    assert up.signature_ok(signed, signed)[0]
    assert not up.signature_ok(signed, unsigned)[0]               # never downgrade to unsigned
    assert not up.signature_ok(signed, ("Valid", "CN=Someone Else"))[0]
    assert not up.signature_ok(unsigned, ("HashMismatch", "CN=X"))[0]


def test_install_kind(tmp_path):
    inst = tmp_path / "Programs" / "F1DockTimers"
    inst.mkdir(parents=True)
    (inst / "unins000.exe").write_text("")
    assert up.install_kind(True, inst / "F1DockTimers.exe", str(inst / "_internal"), None) == "installed"
    port_dir = tmp_path / "Downloads"
    port_dir.mkdir()
    assert up.install_kind(True, port_dir / "F1DockTimers-1.0.2-portable.exe", str(tmp_path / "Temp" / "_MEI1"),
                           None) == "portable"
    folder = tmp_path / "dist" / "F1DockTimers"
    folder.mkdir(parents=True)
    assert up.install_kind(True, folder / "F1DockTimers.exe", str(folder), None) == "other"
    assert up.install_kind(False, Path("python.exe"), None, None) == "other"


def test_sha256(tmp_path):
    f = tmp_path / "x.bin"
    f.write_bytes(b"abc")
    assert up.sha256_file(f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"


def test_update_settings_not_exported(tmp_path):
    from f1win.settings import Settings
    s = Settings(update_notified="1.0.3", skipped_version="1.0.3", update_checked_at=5.0)
    s.export_to(tmp_path / "e.json")
    text = (tmp_path / "e.json").read_text()
    assert "skipped_version" not in text and "update_checked_at" not in text
    assert '"check_updates"' in text


def test_release_with_mismatched_file_versions_is_not_auto_installed():
    # e.g. tag v1.0.2 whose build still says 1.0.1: open the page instead of looping
    rel = release(tag="v1.0.2")
    assert up.pick_asset(rel, "installed") is None
    assert up.pick_asset(rel, "portable") is None


def test_import_accepts_plain_settings_file(tmp_path):
    import json
    from f1win.settings import Settings
    raw = tmp_path / "settings.json"
    raw.write_text(json.dumps({**Settings().__dict__, "taskbar_pill_offset": 120, "notify_lead_min": 30,
                               "widget_geometry": [1, 2, 3, 4]}))
    s = Settings()
    changed = s.import_from(raw)
    assert s.taskbar_pill_offset == 120 and s.notify_lead_min == 30
    assert "widget_geometry" not in changed and s.widget_geometry == []
    junk = tmp_path / "junk.json"
    junk.write_text('{"hello": 1}')
    import pytest
    with pytest.raises(ValueError):
        Settings().import_from(junk)
