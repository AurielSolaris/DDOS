#!/bin/sh
set -eu

root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
mkdir -p "$root/build/musl-obj"
cd "$root/build/musl-obj"
sysroot="$root/build/musl-sysroot"
if command -v cygpath >/dev/null 2>&1; then
    sysroot="$(cygpath -m "$sysroot")"
fi
if [ ! -f config.mak ]; then
    CC='clang --target=i686-unknown-linux-musl -fuse-ld=lld' \
        AR=llvm-ar RANLIB=llvm-ranlib \
        sh ../../.ref/musl/configure --target=i686-unknown-linux-musl --disable-shared \
        --prefix=/usr --syslibdir=/lib
fi
# Match the RPL of the kernel's DPL-2 TLS descriptor.
source=../../.ref/musl/src/thread/i386/__set_thread_area.s
grep -q 'leal 3(,%edx,8),%edx' "$source"
sed 's/leal 3(,%edx,8),%edx/leal 2(,%edx,8),%edx/' "$source" > ddos-thread-area.s
# GNU make writes the archive response file before spawning the archiver.
# This also works with Windows' shorter process command line limit.
cat > archive.mk <<'MAKE'
lib/libc.a:
	$(file >archive.rsp,$(AOBJS))
	llvm-ar rc $@ @archive.rsp
	llvm-ranlib $@
obj/src/thread/i386/__set_thread_area.lo: ddos-thread-area.s
	$(CC) $(CFLAGS_ALL) -c -o $@ $<
MAKE
make -j 8 -f ../../.ref/musl/Makefile -f archive.mk AR=llvm-ar RANLIB=llvm-ranlib
make -f ../../.ref/musl/Makefile -f archive.mk AR=llvm-ar RANLIB=llvm-ranlib \
    DESTDIR="$sysroot" install