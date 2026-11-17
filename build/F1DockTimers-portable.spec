# PyInstaller spec: single-file portable exe (unpacks itself to %TEMP% on each start).
# Same contents as F1DockTimers.spec (the one-folder build used by the installer).
import os

ROOT = os.path.abspath(os.path.join(SPECPATH, ".."))
VERSION = os.environ.get("F1DT_VERSION", "1.0.1")

a = Analysis(
    [os.path.join(ROOT, "launch.pyw")],
    pathex=[ROOT],
    datas=[
        (os.path.join(ROOT, "f1win", "assets"), os.path.join("f1win", "assets")),
        (os.path.join(ROOT, "LICENSE"), "."),
        (os.path.join(ROOT, "THIRD_PARTY_NOTICES.md"), "."),
        (os.path.join(ROOT, "licenses"), "licenses"),
    ],
    hiddenimports=["keyring.backends.Windows", "win32ctypes.core", "tzdata"],
    excludes=["tkinter", "unittest", "pydoc", "PySide6.QtQml", "PySide6.QtQuick", "PySide6.QtWebEngineCore",
              "PySide6.Qt3DCore", "PySide6.QtMultimedia", "PySide6.QtCharts", "PySide6.QtDataVisualization",
              "PySide6.QtPdf", "PySide6.QtSql", "PySide6.QtTest", "PySide6.QtDesigner", "PySide6.QtOpenGL"],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name=f"F1DockTimers-{VERSION}-portable",
    icon=os.path.join(ROOT, "build", "app.ico"),
    version=os.path.join(ROOT, "build", "version_info.txt"),
    console=False,
    disable_windowed_traceback=False,
    upx=False,
    runtime_tmpdir=None,
)
