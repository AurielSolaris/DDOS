"""Build the FreeType modules used by the static DDOS Nano-X server."""

import subprocess
from pathlib import Path


root = Path(__file__).resolve().parent.parent
source = root / ".ref" / "freetype"
build = root / "build"
output = build / "freetype"
constants = {
    "truetype/truetype.c": "FT_Driver_ClassRec, tt_driver_class",
    "cff/cff.c": "FT_Driver_ClassRec, cff_driver_class",
    "psaux/psaux.c": "FT_Module_Class, psaux_module_class",
    "psnames/psnames.c": "FT_Module_Class, psnames_module_class",
    "pshinter/pshinter.c": "FT_Module_Class, pshinter_module_class",
    "sfnt/sfnt.c": "FT_Module_Class, sfnt_module_class",
    "smooth/smooth.c": "FT_Renderer_Class, ft_smooth_renderer_class",
    "raster/raster.c": "FT_Renderer_Class, ft_raster1_renderer_class",
}
extras = (
    "base/ftbase.c", "base/ftinit.c", "base/ftsystem.c", "base/ftdebug.c",
    "base/ftmm.c", "base/ftbbox.c", "base/ftbitmap.c", "base/ftglyph.c", "base/ftstroke.c",
    "base/ftsynth.c", "cache/ftcache.c", "gzip/ftgzip.c",
)


def main():
    if not (source / "include" / "ft2build.h").is_file():
        raise SystemExit("FreeType source is missing. See the desktop build instructions in README.md.")
    output.mkdir(parents=True, exist_ok=True)
    modules = output / "ddos_ftmodule.h"
    modules.write_text("".join(f"FT_USE_MODULE({name})\n" for name in constants.values()),
                       encoding="ascii")
    flags = (
        "-target", "i686-unknown-linux-musl", f"--sysroot={build / 'musl-sysroot'}",
        "-Oz", "-ffunction-sections", "-fdata-sections", "-DFT2_BUILD_LIBRARY",
        "-DFT_CONFIG_MODULES_H=<ddos_ftmodule.h>",
        f"-I{source / 'include'}", f"-I{output}",
    )
    objects = []
    for component in (*constants, *extras):
        obj = output / Path(component).with_suffix(".o").name
        subprocess.run(("clang", *flags, "-c", source / "src" / component, "-o", obj), check=True)
        objects.append(obj)
    subprocess.run(("llvm-ar", "rcs", output / "libfreetype.a", *objects), check=True)


if __name__ == "__main__":
    main()
