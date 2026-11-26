#!/bin/sh
set -eu

root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
reference="$root/.ref/microwindows"
build_dir="$root/build"
revision="$(git -C "$reference" rev-parse --short HEAD)"
source_dir="$build_dir/microwindows-src-lf-$revision"
if [ ! -f "$source_dir/src/Makefile" ]; then
    mkdir -p "$source_dir"
    git -c core.autocrlf=false -c core.eol=lf -C "$reference" archive HEAD | tar -xf - -C "$source_dir" --exclude=README --exclude="*.ttf"
fi
sysroot="$build_dir/musl-sysroot"
gcc_archive="$(find "$sysroot/usr/lib/gcc" -type f -name libgcc.a -print -quit)"
test -n "$gcc_archive"
gcc_install_dir="$(dirname "$gcc_archive")"
if command -v cygpath >/dev/null 2>&1; then
    sysroot="$(cygpath -m "$sysroot")"
    gcc_install_dir="$(cygpath -m "$gcc_install_dir")"
fi

cd "$source_dir/src"
# Build a separate server and client for the DDOS framebuffer.
sed -E -e '/^MOUSE[[:space:]]*=/s/= DEVMICEMOUSE/= DDOSMOUSE/' \
    -e '/^KEYBOARD[[:space:]]*=/s/= SCANKBD/= DDOSKBD/' \
    -e '/^(MICROWIN|MICROWINDEMO|NANOXDEMO|NUKLEARUI|NANOWM|HAVE_SHAREDMEM_SUPPORT|HAVE_PNG_SUPPORT|HAVE_PCF_SUPPORT|HAVE_PCFGZ_SUPPORT|HAVE_FREETYPE_2_SUPPORT|HAVE_FNTGZ_SUPPORT|VTSWITCH|HAVETEXTMODE)[[:space:]]*=/s/= Y/= N/' \
    Configs/config.linux-fb > config.ddos.next
cmp -s config.ddos.next config.ddos || cp config.ddos.next config.ddos
keyboard="$root/user/desktop/keyboard.c"
cmp -s "$keyboard" drivers/kbd_ddos.c || cp "$keyboard" drivers/kbd_ddos.c
mouse="$root/user/desktop/mouse.c"
cmp -s "$mouse" drivers/mou_ddos.c || cp "$mouse" drivers/mou_ddos.c
keys="$root/src/include/console_keys.h"
cmp -s "$keys" drivers/console_keys.h || cp "$keys" drivers/console_keys.h
if ! grep -Fqx 'ifeq ($(KEYBOARD), DDOSKBD)' drivers/Objects.rules; then
    cat >> drivers/Objects.rules <<'MAKE'

ifeq ($(KEYBOARD), DDOSKBD)
MW_CORE_OBJS += $(MW_DIR_OBJ)/drivers/kbd_ddos.o
endif
MAKE
fi
if ! grep -Fqx 'ifeq ($(MOUSE), DDOSMOUSE)' drivers/Objects.rules; then
    cat >> drivers/Objects.rules <<'MAKE'

ifeq ($(MOUSE), DDOSMOUSE)
MW_CORE_OBJS += $(MW_DIR_OBJ)/drivers/mou_ddos.o
endif
MAKE
fi
# Keep NanoWM title bars and borders in the desktop's blue palette.
sed -E \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_ACTIVECAPTION[[:space:]]+\*/)@GR_RGB(50, 82, 128)\1@' \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_ACTIVECAPTIONTEXT[[:space:]]+\*/)@GR_RGB(234, 243, 255)\1@' \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_INACTIVECAPTION[[:space:]]+\*/)@GR_RGB(38, 56, 84)\1@' \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_INACTIVECAPTIONTEXT[[:space:]]+\*/)@GR_RGB(180, 199, 227)\1@' \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_WINDOWFRAMELT[[:space:]]+\*/)@GR_RGB(91, 122, 162)\1@' \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_BTNHIGHLIGHT[[:space:]]+\*/)@GR_RGB(132, 166, 207)\1@' \
    -e 's@GR_RGB\([^)]*\)(.*\/\* GR_COLOR_3DLIGHT[[:space:]]+\*/)@GR_RGB(174, 205, 255)\1@' \
    nanox/nxdraw.c > nanox/nxdraw.ddos
cmp -s nanox/nxdraw.ddos nanox/nxdraw.c || cp nanox/nxdraw.ddos nanox/nxdraw.c
# The standalone server links this archive for the color table.
sed 's@^$(MW_DIR_BIN)/nano-X: $(NANOXFILES) $(CONFIG)$@& $(MW_DIR_LIB)/libnano-X.a@' \
    nanox/Makefile > nanox/Makefile.ddos
cmp -s nanox/Makefile.ddos nanox/Makefile || cp nanox/Makefile.ddos nanox/Makefile
compiler="clang -target i686-unknown-linux-musl --sysroot='$sysroot' --gcc-install-dir='$gcc_install_dir'"
# The server uses nxSysColors even with the internal window manager disabled.
# Link that table from the client library and discard its unused functions.
make -j 8 "CONFIG=$PWD/config.ddos" "COMPILER=$compiler -ffunction-sections -DAUTO_START_SERVER=0" ARCHIVER=llvm-ar \
    'LDFLAGS=-fuse-ld=lld -static -Wl,--gc-sections -lm' \
    'NANOXSERVERLIBS=../lib/libnano-X.a' default
clang -target i686-unknown-linux-musl --sysroot="$sysroot" -Os -Iinclude -Inanox \
    -c demos/nanox/nanowm.c -o bin/nanowm.o
clang -target i686-unknown-linux-musl --sysroot="$sysroot" --gcc-install-dir="$gcc_install_dir" \
    -fuse-ld=lld -static bin/nanowm.o lib/libnano-X.a -lm -o bin/nanowm
include="$root/src/include"
if command -v cygpath >/dev/null 2>&1; then
    include="$(cygpath -m "$include")"
fi
for app in taskbar calculator system_apps file_manager; do
    source="$root/user/desktop/$app.c"
    if command -v cygpath >/dev/null 2>&1; then
        source="$(cygpath -m "$source")"
    fi
    clang -target i686-unknown-linux-musl --sysroot="$sysroot" -Os -Iinclude "-I$include" \
        -c "$source" -o "bin/$app.o"
    clang -target i686-unknown-linux-musl --sysroot="$sysroot" --gcc-install-dir="$gcc_install_dir" \
        -fuse-ld=lld -static "bin/$app.o" lib/libnano-X.a -lm -o "bin/$app"
done
cp bin/system_apps bin/settings
cp bin/system_apps bin/task_mgr
output="$build_dir/microwindows"
mkdir -p "$output"
for name in nano-X nanowm taskbar calculator settings task_mgr file_manager; do
    cp "bin/$name" "$output/$name"
    llvm-strip "$output/$name"
done
