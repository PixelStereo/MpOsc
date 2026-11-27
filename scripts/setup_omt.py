#!/usr/bin/env python3
"""Install the pinned OMT SDK binaries needed by MpOsc."""
import argparse
import hashlib
from pathlib import Path
import platform
import urllib.request
import zipfile

VERSION = "v1.0.0.19"
DIGEST = "c62460174499dabe703e3e57372daf5f1dd95f890355d11e30052b9ed35bdac4"
ARCHIVE = f"OpenMediaTransport.Binaries.Release.{VERSION}.zip"
URL = f"https://github.com/openmediatransport/libomtnet/releases/download/{VERSION}/{ARCHIVE}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    system = platform.system()
    if system == "Darwin":
        folder, extension = "MacOS", "dylib"
    elif system == "Windows":
        folder = "Winarm64" if platform.machine().lower() == "arm64" else "Winx64"
        extension = "dll"
    else:
        parser.error("OMT binaries are available for macOS and Windows")

    names = [f"libomt.{extension}", f"libvmx.{extension}", "OMT-LICENSE.txt"]
    marker = args.destination / "omt-version.txt"
    if marker.is_file() and marker.read_text().strip() == VERSION and all(
        (args.destination / name).is_file() for name in names
    ):
        return

    args.cache.mkdir(parents=True, exist_ok=True)
    archive = args.cache / ARCHIVE
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != DIGEST:
        with urllib.request.urlopen(URL, timeout=60) as response:
            content = response.read()
        if hashlib.sha256(content).hexdigest() != DIGEST:
            raise RuntimeError("Unexpected checksum for the OMT SDK release")
        archive.write_bytes(content)

    args.destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as sdk:
        for name in names:
            member = "LICENSE.txt" if name == "OMT-LICENSE.txt" else f"Libraries/{folder}/{name}"
            (args.destination / name).write_bytes(sdk.read(member))
    marker.write_text(VERSION + "\n")


if __name__ == "__main__":
    main()
