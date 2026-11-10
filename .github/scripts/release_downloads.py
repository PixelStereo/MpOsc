"""Update README links from the newest complete GitHub release."""
import json
import os
from pathlib import Path
import re
import sys

START = "<!-- release-downloads:start -->"
END = "<!-- release-downloads:end -->"
ASSETS = {
    "Download Mac": "MP_OSC-macos-arm64.zip",
    "Download Windows": "MP_OSC-Windows-x64-Setup.exe",
}


def download_links(releases, repository):
    for release in sorted(releases, key=lambda entry: entry.get("published_at") or "", reverse=True):
        tag = release["tag_name"]
        if release["draft"] or not release.get("published_at") or not re.fullmatch(r"v[0-9][0-9A-Za-z.+-]*", tag):
            continue
        uploaded = {asset["name"]: asset["browser_download_url"] for asset in release["assets"]
                    if asset["state"] == "uploaded" and asset["size"] > 0}
        base = f"https://github.com/{repository}/releases/download/{tag}/"
        if all(uploaded.get(filename) == base + filename for filename in ASSETS.values()):
            return " · ".join(f"**[{label}]({base}{filename})**" for label, filename in ASSETS.items())
    raise ValueError("No published release contains both desktop packages")


if __name__ == "__main__":
    pages = json.load(sys.stdin)
    links = download_links((release for page in pages for release in page), os.environ["GITHUB_REPOSITORY"])
    path = Path("README.md")
    text = path.read_text(encoding="utf-8")
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) > text.index(END):
        raise ValueError("Expected one ordered pair of README download markers")
    path.write_text(text[:text.index(START)] + f"{START}\n{links}\n{END}" +
                    text[text.index(END) + len(END):], encoding="utf-8")
