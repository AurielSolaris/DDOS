#!/bin/sh
set -eu

root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
reference="$root/.ref/runit"
build_dir="$root/build"
revision="$(git -C "$reference" rev-parse --short HEAD)"
source_dir="$build_dir/runit-src-$revision"
if [ ! -f "$source_dir/src/Makefile" ]; then
    mkdir -p "$source_dir"
    git -c core.autocrlf=false -c core.eol=lf -C "$reference" archive HEAD | tar -xf - -C "$source_dir"
fi
sysroot="$build_dir/musl-sysroot"
gcc_archive="$(find "$sysroot/usr/lib/gcc" -type f -name libgcc.a -print -quit)"
test -n "$gcc_archive"
gcc_install_dir="$(dirname "$gcc_archive")"
if command -v cygpath >/dev/null 2>&1; then
    sysroot="$(cygpath -m "$sysroot")"
    gcc_install_dir="$(cygpath -m "$gcc_install_dir")"
fi

cd "$source_dir"
compiler="clang -target i686-unknown-linux-musl --sysroot='$sysroot'"
printf '%s\n' "$compiler -O2 -Wall" > src/conf-cc
printf '%s\n' "$compiler --gcc-install-dir='$gcc_install_dir' -fuse-ld=lld -static" > src/conf-ld
# The Linux target has poll and a 32-bit unsigned long. Avoid running the
# target ELF probes on the build host.
sed -e 's/choose clr trypoll/choose cl trypoll/' \
    -e 's@\./choose clr tryulong64 uint64.h1 uint64.h2@cat uint64.h1@' \
    src/Makefile > src/Makefile.ddos
mv src/Makefile.ddos src/Makefile
mkdir -p compile
printf '%s\n' linux-:i386-::-:-:- > compile/systype
sed -e 's@env - PATH=\$PATH pwd@env - "PATH=$PATH" pwd@' \
    -e 's@test -h compile/src || ln -s \$here/src compile/src@test -d compile/src || ln -s "$here/src" compile/src@' \
    -e 's@test -h compile/\$i || ln -s src/\$i compile/\$i@ln -sf "src/$i" "compile/$i"@' \
    package/compile > package/compile-ddos
sh package/compile-ddos

output="$build_dir/runit"
mkdir -p "$output"
for name in runit runit-init runsvdir runsv sv; do
    cp "command/$name" "$output/$name"
    llvm-strip "$output/$name"
done
