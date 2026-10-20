#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Assemble the web flasher site: the page, boards.json, the firmware images and one
ESP Web Tools manifest per board.

  python web/make_site.py <images dir> <version> <output dir>

The images dir holds the release .bin files named as in boards.json.
"""

import json
import shutil
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parent


def main():
    images, version, out = Path(sys.argv[1]), sys.argv[2].lstrip("v"), Path(sys.argv[3])
    boards = json.loads((WEB / "boards.json").read_text(encoding="utf-8"))
    out.mkdir(parents=True, exist_ok=True)
    for name in ("index.html", "boards.json"):
        shutil.copy(WEB / name, out / name)
    for board in boards:
        shutil.copy(images / board["asset"], out / board["asset"])
        manifest = {
            "name": f"XR-DAP ({board['name']['en'].replace(chr(34), '')})",
            "version": version,
            # Upgrades keep the Wi-Fi settings in NVS, as with esptool write_flash.
            "new_install_prompt_erase": False,
            "builds": [
                {
                    "chipFamily": board["chip"],
                    "parts": [{"path": board["asset"], "offset": 0}],
                }
            ],
        }
        (out / f"manifest-{board['id']}.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    print(f"site for {version}: {len(boards)} boards in {out}")


if __name__ == "__main__":
    main()
