# DDOS

DDOS (DOS Derived OS) is a C and NASM operating system based on the published
MS-DOS 4.0 source. It has a 32-bit x86 kernel, BIOS and x64 UEFI boot paths, a
DOS-compatible real-mode program loader, and a persistent ext2 data volume.

## Build and run

Install Python 3, NASM, LLVM with Clang, ld.lld, lld-link, and llvm-objcopy, and
QEMU. The UEFI launch also needs EDK II or OVMF firmware. The build runs natively
on Windows.

```powershell
python build.py
python boot.py          # BIOS with DOS COM execution
python boot.py --uefi   # x64 UEFI with GOP graphics
```

The QEMU launcher attaches three images. The x64 UEFI boot file is
built at `build/uefi/EFI/BOOT/BOOTX64.EFI`. The kernel exposes a four-letter
mount table.

| Drive | Purpose |
| --- | --- |
| `A:/` | FAT12 boot medium at `build/ddos.img`. Its reserved sectors hold the bootloader and flat kernel. |
| `B:/` | Read-only ext2 system image at `build/system.ext2`, with `bin`, `etc`, and `tmp` directories. |
| `C:/` | Reserved for swap. Native path calls return `ENODEV`. |
| `D:/` | Persistent ext2 data image at `build/data.ext2`. Rebuilding preserves it. |

Native absolute paths default to `B:/`. Explicit drive-qualified paths
select their mount. `/dev/null`, `/dev/zero`, and `/dev/console` use kernel
nodes. `/tmp` supports a small RAM file for the current native program.
Relative paths follow the program's current directory. Programs start
in the shell's ext2 data directory when launched with `RUN`.

Use repeatable `--file` options to add DOS programs or other files to
`B:/bin`.

```powershell
python build.py --file path/to/HELLO.COM
```

## Shell and filesystem

The DDOS shell starts on `D:/` when the data volume mounts.
Use drive-qualified paths with forward slashes, such as `D:/documents` or
`B:/bin`. The prompt displays the current directory in this format.
The shell accepts backslashes in existing DOS paths as well.
`HELP` lists the current commands with their syntax.

```text
mkdir D:/documents
cd D:/documents
write hello.txt Hello from DDOS
ls
type hello.txt
cd ..
dir
```

`LS` and `DIR` list a directory. `CD` accepts relative and absolute ext2 paths,
including `..`. Names on ext2 are case-sensitive. `MKDIR` and `WRITE` create
new entries on the DDOS-formatted image. `WRITE` takes a space-free path
followed by optional text. New directory entries use the free space in
existing directory blocks. The DOS handle API also supports writing and
resizing existing regular files within its size limit.

The reader supports ext2's 1, 2, and 4 KiB block sizes, regular files,
directories, sparse blocks, and direct and indirect block pointers.
Compatible external ext2 images mount read-only. Symlink traversal is
unimplemented. The ATA PIO driver reads the system image on the secondary
IDE master and the data image on the primary IDE slave.

## Bootloader and CPU state

| Boot path | Firmware entry and handoff |
| --- | --- |
| BIOS | A NASM boot sector loads the kernel from reserved FAT12 sectors, installs a GDT, and enables protected mode. The kernel uses 32-bit code and data segments. The BIOS bridge temporarily returns to real mode for firmware calls and DOS COM execution. |
| UEFI | `BOOTX64.EFI` is a 64-bit UEFI application. It queries GOP, prefers 1024x768 when available, and passes the selected linear framebuffer to the kernel. After `ExitBootServices`, the assembly entry switches to 32-bit compatibility code, disables paging, clears long mode and PAE, then enters the 32-bit protected-mode kernel. |

The kernel starts with paging and PAE inactive on both paths. The privilege
self-test temporarily turns on non-PAE paging. IA32 (32-bit) UEFI firmware
is unsupported. On real hardware, copy `BOOTX64.EFI` to
`EFI/BOOT/BOOTX64.EFI` on a FAT-formatted EFI system partition and disable
Secure Boot for this unsigned image.

## x86 privilege domains

Run `RINGS` under BIOS or UEFI to exercise the 32-bit privilege setup.

| Ring | What runs |
| --- | --- |
| 0 | Kernel, shell, and the transition handlers |
| 1 | Built-in I/O probe with TSS permission for diagnostic port `0x80` |
| 2 | Built-in user probe with no I/O-port permissions |

Rings 1 and 2 each use a private CR3 page directory and bounded, non-flat
code and data/stack segments. Both run with `IOPL=0`. The TSS bitmap grants
port access for Ring 1 while `CLI` stays privileged. Neither directory maps
the ordinary kernel. The shared transition area contains one read-only
code page, one read-only GDT/IDT/TSS and context page, and one dedicated
interrupt stack page. `CR0.WP` protects the shared read-only pages even
though x86 paging treats Rings 1 and 2 as supervisor. The test uses 4 KiB
non-PAE pages and requires a 486 or newer CPU.

`INT 80h` uses a DPL-2 interrupt gate and the TSS `SS0:ESP0` stack.
Its diagnostic ABI returns the caller's ring in `EAX` when `EAX=0`.
With `EAX=1`, it exits the probe with the value in `EBX`. Other calls
return `-ENOSYS`. A fault returns control to the Ring 0 test harness.

These domains run built-in probes, and Ring 2 also runs the native C program. The existing device drivers
still run in Ring 0, and the BIOS COM loader uses its separate real-mode
path. Preemptive scheduling and driver IPC are pending.

## Native C runtime

Run `RUN` in the DDOS shell to start the default C demo as Ring 2 code.
Build your own small program with `python build.py --app path/to/app.c`
and include `<ddos.h>`. The build links it with the startup code and
minimal C library in `user/` and installs it at `B:/bin/ddosapp`.

Native `INT 80h` uses the Linux i386 syscall numbers and register order.
`EAX` carries the number or result. Arguments are in `EBX`, `ECX`,
`EDX`, `ESI`, `EDI`, and `EBP`. Errors are negative errno values.
The C library covers file I/O, vector writes, seek, heap and anonymous
memory mapping, TLS setup, and basic memory and string routines.

The kernel implements `exit`, `exit_group`, `set_thread_area`,
`set_tid_address`, `execve`, `chdir`, `brk`, `mmap2`, `munmap`,
`openat`, `read`, `write`, `writev`, `close`, `_llseek`, `fstat64`,
and `fstatat64` for this single-program runtime. `openat` and `fstatat64`
support `AT_FDCWD`. `mmap2` supports bounded read/write anonymous memory.
`set_thread_area` supplies one DPL-2 data descriptor. The DDOS C wrapper
loads it with selector RPL 2. Upstream musl's i386 startup uses RPL 3,
which needs an adaptation for DDOS.

The current ELF loader accepts the DDOS link layout in `user/link.ld`,
with one code page and one initial data page. Its bounded startup stack
carries argv, environment strings, and an `AT_PAGESZ` entry. General Linux
ELF loading, process creation and waiting, timer/event syscalls, and the
remaining Linux ABI needed for a musl shell are pending.

### System image and BusyBox

The upstream checkouts are in the ignored `.ref/musl` and `.ref/busybox`
directories. `sh tools/build-musl.sh` builds a static i686 musl sysroot at
`build/musl-sysroot`. `sh tools/build-busybox.sh` starts from
`allnoconfig` and uses [the applet configuration](tools/busybox.config).
It needs a POSIX build shell and host compiler, Clang/LLD on PATH, and
Linux i386 headers and GCC runtime objects in that sysroot. BusyBox
1.36.1 exposes Hush line editing through `FEATURE_EDITING`.

Use `python build.py --busybox build/busybox` to install the static ELF32
as regular files at `B:/bin/busybox` and `B:/bin/sh`. The system image
builder supports direct and indirect ext2 blocks for large host-installed
binaries. On startup DDOS attempts `/bin/sh` with `PATH=/bin` and `D:/`
as its working directory. It returns to the DDOS shell if the executable
is absent, rejected, or exits. Stock BusyBox still requires general ELF
and Linux ABI support to run as PID 1.

## DOS syscall interface

The syscall ABI belongs to real-mode DOS COM programs on the BIOS path.
The loader sets up a minimal PSP and DOS command tail and starts the program
at offset `0100h`. DOS functions use `INT 21h` with the function number in
`AH`. `INT 20h` is also available.

| Vector and function | Input | Behavior |
| --- | --- | --- |
| `INT 20h` or `INT 21h` / `AH=00h` | None | Return to the shell with exit status zero |
| `INT 21h` / `AH=02h` | Character in `DL` | Write the character to the console and return it in `AL` |
| `INT 21h` / `AH=09h` | `DS:DX` points to a `$`-terminated string | Write the string to the console |
| `INT 21h` / `AH=30h` | None | Return DOS version 4.00 in `AL:AH` and clear `BX` and `CX` |
| `INT 21h` / `AH=4Ch` | Exit status in `AL` | Return to the shell with that status |

### ext2 file calls

On the BIOS path, a COM program can use DOS file handles to access ext2.
Use `D:/documents/report.txt` for an absolute path or `report.txt` for a
path relative to the program's current ext2 directory. A COM program starts
with the shell's ext2 directory. Its DOS change-directory calls affect the
program only. DOS lookup folds ASCII case and prefers an exact match.
The native ext2 shell uses case-sensitive names.

For the calls below, carry clear means success. On error, carry is set and
`AX` contains a DOS error code. File handles are local to the running program.

| `AH` | Operation | Registers |
| --- | --- | --- |
| `39h`, `3Bh` | Make or change directory | `DS:DX` points to a null-terminated path |
| `3Ch`, `5Bh` | Create or truncate (`3Ch`) or create a new file exclusively (`5Bh`) | Path in `DS:DX`, normal or archive attribute in `CX`, handle returned in `AX` |
| `3Dh` | Open an existing file | Path in `DS:DX`, access in `AL` (`0` read, `1` write, `2` read/write), handle returned in `AX` |
| `3Eh`, `68h` | Close or commit a handle | Handle in `BX`. Writes are flushed before the call returns |
| `3Fh`, `40h` | Read or write bytes | Handle in `BX`, count in `CX`, buffer in `DS:DX`, bytes transferred in `AX` |
| `42h` | Seek relative to start, current position, or end | `AL` selects origin (`0`/`1`/`2`), handle in `BX`, signed offset in `CX:DX`, new position in `DX:AX` |
| `43h` / `AL=00h` | Read file attributes | Path in `DS:DX`, attributes in `CX` |
| `45h`, `46h` | Duplicate a file handle and share its position | Handle in `BX`, new handle in `AX` for `45h` or target handle in `CX` for `46h` |

A zero-byte write sets the file length to the current position. File writes
support up to 12 KiB on DDOS-formatted ext2 images. Larger files remain
readable. `AH=40h` also writes to console handles `1` and `2`. File rename,
deletion, directory enumeration through Find First/Next, sharing modes,
and FCB calls are unimplemented. Calls outside the implemented set return
carry set and `AX=0001h`. Native protected-mode applications use the separate INT 80h interface.

## VFS and device access

The drive table routes kernel and native paths to FAT12, ext2, and RAM-backed
nodes. The DOS file handle table exposes the ext2 data volume to BIOS-booted
COM programs. DOS console output is available through INT 21h.

The BIOS console uses firmware video and keyboard interrupts through the
real-mode bridge. The UEFI console writes pixels directly to the GOP
framebuffer and polls a PS/2 keyboard. Neither surface is exposed through
`/dev/fb0`, a terminal node, or a keyboard node. The graphics console uses
the public-domain [font8x8 bitmap font](https://github.com/dhepper/font8x8).

## Toolchain and source layout

Clang builds the freestanding kernel for `i386-none-elf` in 32-bit mode.
NASM assembles the BIOS boot sector, x86 entry points, and UEFI payload.
`ld.lld` links the ELF32 kernel with [the x86 linker script](src/arch/x86/kernel.ld).
`llvm-objcopy` extracts the flat kernel images used by the bootloaders.
The x64 UEFI loader is built separately with Clang and linked as a
PE/COFF EFI application by `lld-link`. ELF is a build intermediate.
The DOS loader runs 16-bit flat COM binaries from B:/bin on BIOS. Native
applications use a limited static ELF32 loader. DOS MZ EXE loading is pending.

| Directory | Responsibility |
| --- | --- |
| `src/boot/bios`, `src/boot/uefi` | Firmware entry and platform services |
| `src/arch/x86` | Protected-mode support and linker script |
| `src/drivers` | ATA disk I/O and the console bitmap font |
| `src/fs` | VFS drive routing, FAT12, and ext2 |
| `src/kernel` | Shell, console helpers, DOS COM loader, and DOS file handles |
| `user` | Native startup code, C library, linker script, and demo |
| `src/include`, `tools` | Shared interfaces and the host-side ext2 formatter |

The local upstream reference is in `.ref/MS-DOS/v4.0/src`. The Microsoft
source is published under the MIT License. DDOS currently implements a
subset of MS-DOS 4.0. Most DOS interrupts, drivers, and utilities remain
unported.