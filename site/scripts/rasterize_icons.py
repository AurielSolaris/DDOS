"""Rasterize the DDOS icon into the PNG sizes listed in the web app manifest."""

import json
from pathlib import Path

import resvg_py


def main():
    public = Path(__file__).resolve().parents[1] / "public"
    source = public / "icons" / "ddos.svg"
    manifest = json.loads((public / "manifest.webmanifest").read_text(encoding="utf-8"))
    for icon in manifest["icons"]:
        if icon["type"] != "image/png":
            continue
        width, height = map(int, icon["sizes"].split("x"))
        png = resvg_py.svg_to_bytes(svg_path=str(source), width=width, height=height)
        (public / icon["src"]).write_bytes(png)


if __name__ == "__main__":
    main()
