#!/bin/sh
set -eu

root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
reference="$root/.ref/busybox"
build_dir="$root/build"
revision="$(git -C "$reference" rev-parse --short HEAD)"
source_dir="$build_dir/busybox-src-lf-$revision"
if [ ! -f "$source_dir/Makefile" ]; then
    mkdir -p "$source_dir"
    git -c core.autocrlf=false -c core.eol=lf -C "$reference" archive HEAD | tar -xf - -C "$source_dir"
fi
sysroot="$build_dir/musl-sysroot"
target_sysroot="$sysroot"
if command -v cygpath >/dev/null 2>&1; then
    target_sysroot="$(cygpath -m "$sysroot")"
fi
gcc_version_dir="$(find "$sysroot/usr/lib/gcc" -type f -name libgcc.a -print -quit)"
gcc_install_dir="$(dirname "$gcc_version_dir")"
if command -v cygpath >/dev/null 2>&1; then
    gcc_install_dir="$(cygpath -m "$gcc_install_dir")"
fi

fragment="$build_dir/busybox.allnoconfig"
tr -d '\r' < "$root/tools/busybox.config" > "$fragment"
printf '\nCONFIG_SYSROOT="%s"\n' "$target_sysroot" >> "$fragment"
cd "$source_dir"
make allnoconfig
awk -F= '
    NR == FNR && /^CONFIG_/ {wanted[$1] = $0; next}
    NR == FNR {next}
    {
        key = $1
        if ($0 ~ /^# CONFIG_.* is not set$/) {
            key = $0
            sub(/^# /, "", key)
            sub(/ is not set$/, "", key)
        }
        if (key in wanted) {
            print wanted[key]
            delete wanted[key]
        } else {
            print
        }
    }
    END {for (key in wanted) print wanted[key]}
' "$fragment" .config > .config.ddos
mv .config.ddos .config
yes '' | make oldconfig
for option in HUSH HUSH_INTERACTIVE FEATURE_EDITING SH_IS_HUSH FEATURE_SH_STANDALONE \
    BC DC DATE TIME SLEEP LS CAT CP MV RM MKDIR RMDIR PWD ECHO CLEAR GREP HEAD TAIL WC TEST TRUE FALSE; do
    grep -qx "CONFIG_$option=y" .config
done
make -j 8 "CC=clang --target=i686-unknown-linux-musl --gcc-install-dir=$gcc_install_dir" \
    AR=llvm-ar NM=llvm-nm STRIP=llvm-strip
cp busybox "$build_dir/busybox"
cp .config "$build_dir/busybox.config"