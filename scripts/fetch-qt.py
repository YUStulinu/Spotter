"""Downloads the open-source Qt 6.11.3 for Windows (MSVC 2022, x64) into third_party/qt: the base,
Qt Quick and its controls, SVG and the tools (moc, qmlcachegen), straight from download.qt.io, each
archive checked against the SHA-1 the repository publishes.

    python scripts/fetch-qt.py      (needs: pip install py7zr)

Any Qt 6.8 or newer works as well: pass its folder to CMake (CMAKE_PREFIX_PATH) instead.
"""

from __future__ import annotations

import hashlib
import sys
import urllib.request
from pathlib import Path

import py7zr

ROOT = Path(__file__).resolve().parents[1]
REPO = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6113/qt6_6113_msvc2022_64"
VERSION = "6.11.3-0-202610220312"
PLATFORM = "-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z"
PACKAGE = "qt.qt6.6113.win64_msvc2022_64"
ARCHIVES = [f"{m}{PLATFORM}" for m in ("qtbase", "qtsvg", "qtdeclarative", "qttools")] + ["d3dcompiler_47-x64.7z"]


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url) as r:  # download.qt.io redirects to a mirror
        return r.read()


def main():
    out = ROOT / "third_party" / "qt"
    if (out / "bin" / "Qt6Core.dll").exists():
        print(f"Qt is in {out}")
        return
    dl = ROOT / "third_party" / "downloads" / "qt"
    dl.mkdir(parents=True, exist_ok=True)
    for name in ARCHIVES:
        file = dl / name
        url = f"{REPO}/{PACKAGE}/{VERSION}{name}"
        sha1 = fetch(url + ".sha1").decode().split()[0]
        if not file.exists() or hashlib.sha1(file.read_bytes()).hexdigest() != sha1:
            print(f"downloading {name}", flush=True)
            data = fetch(url)
            if hashlib.sha1(data).hexdigest() != sha1:
                sys.exit(f"checksum mismatch: {name}")
            file.write_bytes(data)
        with py7zr.SevenZipFile(file) as z:
            z.extractall(out)
    print(f"Qt is in {out}")


if __name__ == "__main__":
    main()
