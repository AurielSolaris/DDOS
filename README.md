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
python boot.py          # BIOS with DOS COM and MZ EXE execution
python boot.py --uefi   # x64 UEFI with GOP graphics
```

Build and boot the UEFI desktop with these commands.
```text
sh tools/build-microwindows.sh
python build.py --desktop
python boot.py --uefi
```

At the DDOS prompt, enter `NANOWM`. Click the **DDOS** button at the left
of the taskbar to open the app menu. Type a search and press Enter to open
the selected result, or click any visible result. Tab cycles through
matches. Calculator, Settings, and Task Manager also have desktop icons.
**Restart** and **Shutdown** are available at the bottom of the popup on
the default QEMU PC. Esc returns to the DDOS prompt.

Settings shows firmware, display, and installed guest RAM information.
Task Manager refreshes every second and reports mapped RAM per native
process and for the 8 MiB task arena, alongside QEMU's installed RAM.
Both apps have NanoWM title bars so they can be moved or closed.

Press F2 to open Calculator or bring its window forward. Type `CALC`
from the DDOS prompt, or use `python boot.py --calculator` to open it
directly. Calculator supports mouse input and the number and operator
keys. Enter evaluates, Backspace deletes, N changes the sign, C clears,
and Q closes Calculator.

The taskbar displays date and time in UTC from the PC clock. Add
`--debug-log build/qemu.log` to the boot command to capture guest
diagnostics. QEMU uses a temporary copy of the system image, so the build
can replace it while the VM runs. If an older VM holds the original file
open, pass `--system-image build/system-desktop.ext2` to both scripts.

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
select their mount. `/dev/null` and `/dev/zero` use kernel nodes. `/dev/console` and
`/dev/tty` both connect to the DDOS console. UEFI exposes `/dev/fb0`
when the GOP framebuffer fits the mapping described below. `/tmp` supports a small RAM file shared by tasks in the current native session.
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
`HELP` lists the current commands with their syntax. Use `SHUTDOWN`
to power off or `REBOOT` to restart the default QEMU PC, on BIOS or UEFI.

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
| BIOS | A NASM boot sector loads the kernel from reserved FAT12 sectors, installs a GDT, and enables protected mode. The kernel uses 32-bit code and data segments. The BIOS bridge temporarily returns to real mode for firmware calls and DOS COM or MZ execution. |
| UEFI | `BOOTX64.EFI` is a 64-bit UEFI application. It queries GOP, prefers the smallest supported mode at or above 1280x720, and passes the selected linear framebuffer to the kernel. After `ExitBootServices`, the assembly entry switches to 32-bit compatibility code, disables paging, clears long mode and PAE, then enters the 32-bit protected-mode kernel. |

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
the ordinary kernel. The shared transition area contains a read-only
page with the entry code and GDT/IDT/TSS metadata, plus a dedicated
interrupt stack page. `CR0.WP` protects the shared read-only pages even
though x86 paging treats Rings 1 and 2 as supervisor. The test uses 4 KiB
non-PAE pages and requires a 486 or newer CPU.

`INT 80h` uses a DPL-2 interrupt gate and the TSS `SS0:ESP0` stack.
Its diagnostic ABI returns the caller's ring in `EAX` when `EAX=0`.
With `EAX=1`, it exits the probe with the value in `EBX`. Other calls
return `-ENOSYS`. A fault returns control to the Ring 0 test harness.

These domains run built-in probes, and Ring 2 also runs the native C program. The existing device drivers
still run in Ring 0, and the BIOS DOS loader uses its separate real-mode
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

| Interface | Implemented behavior |
| --- | --- |
| Processes | `fork`, `execve`, `wait4` for a child PID or any child, `exit`, `exit_group`, PID and parent lookup |
| Signals | Standard signals with `rt_sigaction`, `rt_sigprocmask`, legacy `sigreturn`, and `kill`. Supports simple handlers and SIGCHLD delivery. |
| Memory and TLS | `brk`, `mmap2`, `munmap`, `set_thread_area`, and `set_tid_address` |
| Files and descriptors | `open`, `openat`, `read`, `write`, `writev`, `close`, `dup`, `dup2`, `_llseek`, `stat64` family, `chdir`, `mkdir`, and `mkdirat`. Path syscalls with a directory descriptor accept `AT_FDCWD`. |
| IPC and timing | `pipe`, `pipe2`, `poll`, `_newselect`, relative `nanosleep`, and monotonic or RTC-backed `clock_gettime`. `fcntl` supports descriptor duplication, nonblocking I/O, append, and close-on-exec. |

The scheduler switches among up to eight Ring 2 processes at syscalls.
Each task has private memory, register state, and TLS. Forked descriptors
share file offsets and status flags. Blocking pipe and console I/O and
timer waits let other tasks run. Execution is cooperative. A program
that never makes a syscall can hold the CPU. BIOS native tasks require
at least 9 MiB of contiguous extended memory reported by the firmware.

`set_thread_area` supplies one DPL-2 data descriptor. The DDOS C library
and the locally built musl archive load it with selector RPL 2.

The native loader reads static i386 ELF32 executables directly from ext2.
It maps multiple PT_LOAD segments at their linked addresses, zeros BSS,
leaves gaps unmapped, and passes argv, environment, and the program-header
metadata in the auxiliary vector. Each task has a 1 MiB image window
including stack and heap. The original DDOS link layout retains its
compact memory layout. The bundled musl programs target i686 with SSE2.
Dynamic linking and PIE are pending.

### Local pipes and Nano-X transport

Musl and BusyBox use byte-stream pipes through the Linux syscall ABI.
DDOS also accepts case-insensitive Windows-style names such as
`\\.\pipe\desktop`. Open a name with `O_CREAT | O_EXCL | O_RDWR` to
create a server endpoint, then open the same name with `O_RDWR` for one
duplex client connection. Closing the peer allows pending data to drain
before EOF. DDOS supports eight pipe pairs with 512 bytes per direction.

The local `AF_UNIX` stream adapter routes `socket`, `bind`, `listen`,
`connect`, and `accept` to the same pipe queues. Nano-X can use its
existing local socket protocol. Named pipes and sockets expose distinct
interfaces, similar to the split in Wine.
[Wine named pipes](https://github.com/wine-mirror/wine/blob/master/server/named_pipe.c) and
[Wine sockets](https://github.com/wine-mirror/wine/blob/master/server/sock.c).

### BusyBox, runit, and Nano-X

Place the upstream source trees in the ignored `.ref/musl`,
`.ref/busybox`, `.ref/runit`, and `.ref/microwindows` directories. Use
[upstream musl](https://git.musl-libc.org/git/musl/),
[BusyBox 1.36.1](https://busybox.net/),
[runit](https://github.com/g-pape/runit), and
[Microwindows](https://github.com/ghaerr/microwindows). Use
a POSIX build shell, GNU Make, a host C compiler, and LLVM on PATH. The
i686 musl sysroot also needs Linux UAPI headers and matching GCC runtime
objects for Clang's static links. The scripts work with native Windows
LLVM and an MSYS shell and host tools.

```sh
sh tools/build-musl.sh
sh tools/build-busybox.sh
sh tools/build-runit.sh
sh tools/build-microwindows.sh
python build.py
```

The builder installs the available static ELF32 outputs as regular files
in `B:/bin`. Use `--busybox FILE`, `--runit DIR`, or `--microwindows DIR`
to use explicitly supplied binaries. BusyBox provides `busybox` and
`sh` with the [Hush and applet configuration](tools/busybox.config).
The runit tools are `runit`, `runit-init`, `runsvdir`, `runsv`, and `sv`.
The graphics build installs `nano-X`, `nanowm`, `taskbar`, `calculator`,
`settings`, and `task_mgr`.

The native loader also accepts scripts whose shebang names an absolute
ELF interpreter path. The image includes executable runit stages in
`B:/etc/runit`, the login shell service in `B:/etc/service/shell/run`,
and a login profile that checks for `/dev/fb0`.

Boot currently goes directly to the DDOS prompt. Runit remains installed
in B: for development. PID 1 supervision needs writable service state,
directory enumeration, terminal control, and `getcwd`. Use `NANOWM`
from the DDOS prompt to open the UEFI desktop.

## DOS syscall interface

The syscall ABI belongs to DOS programs on the BIOS path. The loader sets up
a minimal PSP and DOS command tail. It starts COM programs at offset `0100h`
and uses the MZ header's relocated `CS:IP` and `SS:SP` for EXE programs.
MZ load images and their minimum allocation must fit in the existing
64 KiB DOS program arena. This path uses the real-mode bridge. Virtual
8086 execution is pending. DOS functions use `INT 21h` with the function number in
`AH`. `INT 20h` is also available.

| Vector and function | Input | Behavior |
| --- | --- | --- |
| `INT 20h` or `INT 21h` / `AH=00h` | None | Return to the shell with exit status zero |
| `INT 21h` / `AH=02h` | Character in `DL` | Write the character to the console and return it in `AL` |
| `INT 21h` / `AH=09h` | `DS:DX` points to a `$`-terminated string | Write the string to the console |
| `INT 21h` / `AH=30h` | None | Return DOS version 4.00 in `AL:AH` and clear `BX` and `CX` |
| `INT 21h` / `AH=4Ch` | Exit status in `AL` | Return to the shell with that status |

### ext2 file calls

On the BIOS path, a DOS program can use DOS file handles to access ext2.
Use `D:/documents/report.txt` for an absolute path or `report.txt` for a
path relative to the program's current ext2 directory. A DOS program starts
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
DOS programs. DOS console output is available through INT 21h.

The BIOS console uses firmware video and keyboard interrupts through the
real-mode bridge. The UEFI console writes pixels directly to the GOP
framebuffer and polls a PS/2 keyboard. Native programs can use
`/dev/console` or `/dev/tty` for console I/O. `/dev/fb0` supports Linux
`FBIOGET_FSCREENINFO`, `FBIOGET_VSCREENINFO`, and a shared full-device
mapping for a page-aligned GOP framebuffer of up to 8 MiB. The UEFI
PS/2 driver separates keyboard and mouse traffic and presents the
mouse as `/dev/mouse0` when the auxiliary device responds. Reads return
one three-byte PS/2 report with relative movement and button state.
The Nano-X driver supports left, right, and middle buttons. See the
[Linux PS/2 mouse driver](https://github.com/torvalds/linux/blob/master/drivers/input/mouse/psmouse-base.c)
and [FreeBSD psm driver](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/atkbdc/psm.c).

The graphics console uses
the public-domain [font8x8 bitmap font](https://github.com/dhepper/font8x8).

## Toolchain and source layout

Clang builds the freestanding kernel for `i386-none-elf` in 32-bit mode.
NASM assembles the BIOS boot sector, x86 entry points, and UEFI payload.
`ld.lld` links the ELF32 kernel with [the x86 linker script](src/arch/x86/kernel.ld).
`llvm-objcopy` extracts the flat kernel images used by the bootloaders.
The x64 UEFI loader is built separately with Clang and linked as a
PE/COFF EFI application by `lld-link`. ELF is a build intermediate.
The DOS loader runs 16-bit COM and small MZ EXE binaries from `B:/bin`
on BIOS. Native applications use the static ELF32 loader.

| Directory | Responsibility |
| --- | --- |
| `src/boot/bios`, `src/boot/uefi` | Firmware entry and platform services |
| `src/arch/x86` | Protected-mode support and linker script |
| `src/drivers` | ATA disk I/O and the console bitmap font |
| `src/fs` | VFS drive routing, FAT12, and ext2 |
| `src/kernel` | Shell, console helpers, DOS executable loader and DOS file handles |
| `user` | Native startup code, C library, linker script, and demo |
| `src/include`, `tools` | Shared interfaces and the host-side ext2 formatter |

## Component provenance

| Component | Origin and DDOS changes |
| --- | --- |
| DOS shell, COM/MZ loader, and INT 21h file calls | DDOS C/NASM implementation of a subset of DOS conventions, using the published [Microsoft MS-DOS 4.0 source](https://github.com/microsoft/MS-DOS/tree/main/v4.0) as a local reference. The drive-letter paths, DOS command names, PSP, and interrupt conventions come from DOS. |
| BIOS boot sector and UEFI boot application | DDOS boot code. BIOS loads the flat kernel from reserved FAT12 sectors. The UEFI application uses the firmware GOP interface, then hands off to the 32-bit kernel. [UEFI specifications](https://uefi.org/specifications). |
| Kernel, processes, VFS, and system image builder | DDOS implementations in C, NASM, and Python. FAT12, ATA PIO, ext2, and static ELF32 use their underlying formats and interfaces. DDOS combines drive letters with POSIX absolute paths. |
| PS/2, framebuffer, and guest power | DDOS drivers and QEMU integration. The PS/2 packet handling was informed by [Linux psmouse](https://github.com/torvalds/linux/blob/master/drivers/input/mouse/psmouse-base.c) and [FreeBSD psm](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/atkbdc/psm.c). The QEMU launcher uses [firmware configuration](https://www.qemu.org/docs/master/specs/fw_cfg.html), and guest shutdown uses [QEMU's PIIX4 ACPI controller](https://github.com/qemu/qemu/blob/master/hw/acpi/piix4.c). |
| Native ABI, pipes, and local sockets | DDOS runtime with a subset of the Linux i386 syscall ABI for musl. The named-pipe namespace follows Windows naming and the interface split was informed by [Wine](https://github.com/wine-mirror/wine/blob/master/server/named_pipe.c). DDOS implements its own queues and local socket adapter. |
| C libraries | The small freestanding library in `user/` is DDOS code. The optional static libc is [upstream musl](https://git.musl-libc.org/git/musl/) with a local i386 TLS selector adjustment for Ring 2. |
| BusyBox and runit | Upstream [BusyBox](https://busybox.net/) supplies Hush and its configured applets. Upstream [runit](https://github.com/g-pape/runit) is cross-built by DDOS. The applet configuration, stage scripts, shell service, and image integration are local. |
| Graphics server and window manager | [Microwindows](https://github.com/ghaerr/microwindows) supplies Nano-X and the floating NanoWM. DDOS provides its input adapters, build configuration, desktop startup, and blue window colors. |
| App menu, taskbar, Calculator, Settings, Task Manager, and icons | Original DDOS desktop components. Their blue tonal palette uses [Material 3 color roles](https://m3.material.io/styles/color/the-color-system). The layered tint and highlights adapt [Apple HIG materials guidance](https://developer.apple.com/design/human-interface-guidelines/materials) to Nano-X drawing. |
| Console typeface | Public-domain [font8x8](https://github.com/dhepper/font8x8), included in the graphics console. |
| Toolchain and emulation | Python build and boot wrappers are DDOS code. Clang, LLD, NASM, QEMU, and EDK II/OVMF are external tools or firmware. |

The repository's [MIT license](LICENSE) covers the DDOS project files as
described there. Upstream projects retain their own licenses. Reference
checkouts in `.ref/` and generated binaries and images in `build/` are
excluded from Git.
