#!/bin/sh
set -eu

root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
mkdir -p "$root/build/musl-obj"
cd "$root/build/musl-obj"
sysroot="$root/build/musl-sysroot"
if command -v cygpath >/dev/null 2>&1; then
    sysroot="$(cygpath -m "$sysroot")"
fi
CC='clang --target=i686-unknown-linux-musl -fuse-ld=lld' \
    AR=llvm-ar RANLIB=llvm-ranlib \
    sh ../../.ref/musl/configure --target=i686-unknown-linux-musl --disable-shared \
    --prefix=/usr --syslibdir=/lib
# GNU make writes the archive response file before spawning the archiver.
# This also works with Windows' shorter process command line limit.
cat > archive.mk <<'MAKE'
lib/libc.a:
	$(file >archive.rsp,$(AOBJS))
	llvm-ar rc $@ @archive.rsp
	llvm-ranlib $@
MAKE
make -j 8 -f ../../.ref/musl/Makefile -f archive.mk AR=llvm-ar RANLIB=llvm-ranlib
make -f ../../.ref/musl/Makefile -f archive.mk AR=llvm-ar RANLIB=llvm-ranlib \
    DESTDIR="$sysroot" install