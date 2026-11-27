# DDOS

DDOS (DOS Derived OS) is a C and NASM operating system based on the published
MS-DOS 4.0 source. It has a 32-bit x86 kernel, BIOS and x64 UEFI boot paths, a
DOS-compatible real-mode program loader, and a persistent ext2 data volume.

## Project website

The [site directory](site/) contains the DDOS project website, built with
React, TanStack Router, SCSS, and a WebGL wallpaper with a CSS fallback.
It includes an interactive desktop preview with draggable app windows,
a keyboard-driven simulated terminal, and a QEMU build guide.

```sh
cd site
bun install
bun run dev
```

Run `bun run build` to generate `site/dist/`. For Vercel, set the Root
Directory to `site`. See the [website README](site/README.md) for details.

## Build and run

Install Python 3, NASM, LLVM with Clang, ld.lld, lld-link, and llvm-objcopy, and
QEMU. The UEFI launch also needs EDK II or OVMF firmware. The build runs natively
on Windows. The QEMU launcher defaults to 4 GiB of guest RAM and
512 MiB of emulated Standard VGA memory. Override these defaults with
`--ram-mib` and `--vram-mib`, for example
`python boot.py --uefi --ram-mib 512 --vram-mib 32`.
The glass effect uses system RAM and the software renderer.

```powershell
python build.py
python boot.py          # BIOS with DOS COM and MZ EXE execution
python boot.py --uefi   # x64 UEFI with GOP graphics
```

On a fresh checkout, set up the [desktop build dependencies](#busybox-runit-and-nano-x)
before building the UEFI desktop.
```text
sh tools/build-microwindows.sh
python build.py --desktop
python boot.py --uefi
```

To import a downloaded wallpaper, install Pillow and rebuild the system image
with the desktop binaries already built.

```powershell
python -m pip install Pillow
python build.py --desktop --wallpaper "C:\Pictures\wallpaper.jpg"
python boot.py --uefi
```

The importer accepts JPEG, PNG, WebP, and other Pillow-readable formats. It
center-crops to 16:9 and stores a 320x180 BMP on B:. Nano-X scales it to the
screen, and the frosted panels sample its colors. For a persistent override,
place an uncompressed, bottom-up 24-bit BMP up to 320x180 at
`D:/wallpaper.bmp` and reopen NanoWM. With neither image present, DDOS uses
its built-in blue background. The repository ships the built-in background.

At the DDOS prompt, enter `NANOWM`. Click the **DDOS** button at the left
of the taskbar to open the app menu. Type a search and press Enter to open
the selected result, or click any visible result. Tab cycles through
matches. Calculator, Settings, Task Manager, File Manager, Notepad, and
Snake also have desktop icons. Desktop and launcher icons are larger,
and NanoWM title bars, window borders, and close buttons have more room.
Buttons, cards, desktop tiles, and the launcher use rounded corners.
The dark blue panels sample the screen behind them when they open,
blur the sample, and blend it with a navy tint. The launcher refreshes
its sample on every opening. Open panels retain their snapshot when
they move. **Restart** and **Shutdown** are available
at the bottom of the popup on the default QEMU PC. Esc returns to the DDOS
prompt. Win/Cmd+R opens the centered, keyboard-first launcher. Type a
name or part of one, use the arrow keys or Tab to select, and press Enter.
The ranking prefers whole and prefix matches, then matches typed letters
in order. F3 also opens the launcher if the host intercepts Win/Cmd+R.

Settings shows the processor, firmware, PCI display adapter ID, display
mode, firmware-reported RAM, and the EDOS (Extended DOS Kernel) name.
Its render modes apply immediately. **Auto** uses SSE2 for eligible
shadow-buffer copies when the processor supports it. **Software**
uses the scalar copy. **CPU accelerated** explicitly selects SSE2
when available. Use the buttons or keys 1, 2, and 3. Text still uses
the CPU, with grayscale antialiasing for scalable fonts. GPU
acceleration requires a DDOS driver for the detected display adapter.
Use **Window scale** to shrink or enlarge open DDOS desktop apps, the
taskbar, and desktop shortcuts. The range is 75% through 125% in 5%
steps. Use Left and Right to step through sizes, or press 0 or choose
**Reset 100%** to restore the default. The scale lasts for the
current desktop session. NanoWM title bars keep their fixed height and
the GOP resolution stays the same.
**Load font** accepts a guest path such as `D:/fonts/my-font.otf` or
`D:/fonts/my-font.woff`. Press Enter to apply, or F4 to cancel.
**Restore Inter** switches back to the bundled UI font. The loader
supports OpenType and WOFF 1.0 with guest paths up to 63 characters.
The choice lasts for the desktop session.

Task Manager refreshes its memory view automatically and reports mapped
RAM per native process and for the 25 MiB desktop task arena, alongside QEMU's
installed RAM. Use Page Up and Page Down when the list spans pages.

Press F2 to open Calculator or bring its window forward. Type `CALC`
from the DDOS prompt, or use `python boot.py --calculator` to open it
directly. Calculator supports mouse input and the number and operator
keys. Enter evaluates, Backspace deletes, N changes the sign, C clears,
and Q closes Calculator.

File Manager opens on `D:/`. Use the drive buttons to browse `B:/`,
`D:/`, or a mounted USB disk on `E:/`. Double-click a folder to open
it or a file to open it in Notepad. Enter opens the selected entry.
Right-click an entry for Open, Copy, Rename, Delete, and Paste, or
right-click empty space for New file, New folder, Refresh, and Paste.
New file creates an empty file. Rename and file or folder creation
accept names in the footer. Enter confirms and F4 cancels. Delete
removes files and empty folders. Copy and Paste transfer regular files
between writable destinations without overwriting existing files.

Notepad edits plain ASCII text files up to 32 KiB. Use Ctrl+N, Ctrl+O, and
Ctrl+S for New, Open, and Save, or use the toolbar to save under a new
name. It accepts LF and CRLF on input and saves with LF line endings.
Closing or replacing a document with unsaved edits prompts for a decision.

Snake uses the arrow keys or WASD to steer and Space to pause or resume.
It wraps across all four edges. Hitting its own body ends the game.
Use Start or Enter to begin. The game tracks your best score while its
window is open.

The taskbar displays date and time in UTC from the PC clock. Add
`--debug-log build/qemu.log` to the boot command to capture guest
diagnostics. QEMU uses a temporary copy of the system image, so the build
can replace it while the VM runs. If an older VM holds the original file
open, pass `--system-image build/system-desktop.ext2` to both scripts.

The QEMU launcher attaches three images. The x64 UEFI boot file is
built at `build/uefi/EFI/BOOT/BOOTX64.EFI`. The kernel reserves
`E:/` for an optional USB disk.

| Drive | Purpose |
| --- | --- |
| `A:/` | FAT12 boot medium at `build/ddos.img`. Its reserved sectors hold the bootloader and flat kernel. |
| `B:/` | Read-only ext2 system image at `build/system.ext2`, with `bin`, `etc`, and `tmp` directories. |
| `C:/` | Reserved for swap. Native path calls return `ENODEV`. |
| `D:/` | Persistent ext2 data image at `build/data.ext2`. Rebuilding preserves it. |
| `E:/` | Optional ext2 USB 2.0 mass-storage volume detected at boot. Native path calls return `ENODEV` when absent. |

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
followed by optional text. Use `DEL` for files, `RMDIR` for empty
directories, and `REN` for same-drive renames. Renaming to a name already
in use fails. DDOS can expand a writable directory up to 12 blocks.
The DOS and native file APIs can write through single and double indirect
blocks. The write path supports logical files up to 8 MiB. The default
4 MiB data image limits how much file data can be allocated, and disk
space is shared across all of its files and metadata.

The reader supports ext2's 1, 2, and 4 KiB block sizes, regular files,
directories, sparse blocks, and direct and indirect block pointers.
Compatible external ext2 images mount read-only. Symlink traversal is
unimplemented. The block layer dispatches sector reads, writes, and
flushes to both ATA and USB drivers. The ATA PIO driver accesses the
system image on the secondary IDE master and the data image on the
primary IDE slave.

### USB 2.0 storage

Create an optional raw USB disk and attach it to QEMU with these commands.

```powershell
python -c "from pathlib import Path; from tools.mkext2 import create_if_missing; create_if_missing(Path('build/usb.ext2'))"
python boot.py --uefi --usb-image build/usb.ext2
```

The EHCI driver probes PCI bus 0 and uses polled control and bulk transfers.
DDOS mounts one directly connected high-speed device with a 512-byte
logical sector, SCSI transparent commands, and Bulk-Only Transport.
The ext2 filesystem starts at sector zero. Only the supported DDOS data
layout is writable. Partition tables, USB hubs, and runtime hotplug are
pending.

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
path. Driver IPC is pending.

## Native C runtime

Run `RUN` in the DDOS shell to start the default C demo as Ring 2 code.
Build your own small program with `python build.py --app path/to/app.c`
and include `<ddos.h>`. The build links it with the startup code and
minimal C library in `user/` and installs it at `B:/bin/ddosapp`.

Native `INT 80h` uses the Linux i386 syscall numbers and register order.
`EAX` carries the number or result. Arguments are in `EBX`, `ECX`,
`EDX`, `ESI`, `EDI`, and `EBP`. Errors are negative errno values.
The C library covers file I/O, vector reads and writes, seek, heap and anonymous
memory mapping, TLS setup, and basic memory and string routines.

| Interface | Implemented behavior |
| --- | --- |
| Processes | `fork`, `execve`, `wait4` for a child PID or any child, `exit`, `exit_group`, PID and parent lookup |
| Signals | Standard signals with `rt_sigaction`, `rt_sigprocmask`, legacy `sigreturn`, and `kill`. Supports simple handlers and SIGCHLD delivery. |
| Memory and TLS | `brk`, `mmap2`, `munmap`, `set_thread_area`, and `set_tid_address` |
| Files and descriptors | `open`, `openat`, `read`, `readv`, `write`, `writev`, `close`, `dup`, `dup2`, `_llseek`, `stat64` family, `chdir`, `mkdir(at)`, `getdents64`, `unlink(at)`, `rmdir`, and `rename(at/at2)`. Path syscalls with a directory descriptor accept `AT_FDCWD`. Open entries and active working directories return `EBUSY` on deletion. Renames preserve existing destination files and return `EXDEV` across volumes. |
| IPC and timing | `pipe`, `pipe2`, `poll`, `_newselect`, relative `nanosleep`, and monotonic or RTC-backed `clock_gettime`. `fcntl` supports descriptor duplication, nonblocking I/O, append, and close-on-exec. |

The scheduler runs up to 24 Ring 2 processes. A PIT interrupt preempts
user code about every 10 ms. Each task has private memory, register and
FPU state, and TLS. Forked descriptors share offsets and status flags.
System calls remain non-preemptible in Ring 0. Blocking pipe and console
I/O and timer waits yield to other tasks. File writes may return a
partial count so the kernel can schedule other tasks between chunks.
BIOS requires about 27 MiB of contiguous extended memory for the native
task images, executable staging, and runtime metadata.

`set_thread_area` supplies one DPL-2 data descriptor. The DDOS C library
and the locally built musl archive load it with selector RPL 2.

The native loader reads static i386 ELF32 executables directly from ext2.
It maps multiple PT_LOAD segments at their linked addresses, zeros BSS,
leaves gaps unmapped, and passes argv, environment, and the program-header
metadata in the auxiliary vector. Apps have 1 MiB image windows
including stack and heap. The desktop server has a 2 MiB image window
for its rendering and font engine. The original DDOS link layout retains its
compact memory layout. The bundled musl programs target i686 with SSE2.
Dynamic linking and PIE are pending.

### Local pipes and Nano-X transport

Musl and BusyBox use byte-stream pipes through the Linux syscall ABI.
DDOS also accepts case-insensitive Windows-style names such as
`\\.\pipe\desktop`. Open a name with `O_CREAT | O_EXCL | O_RDWR` to
create a server endpoint, then open the same name with `O_RDWR` for one
duplex client connection. Closing the peer allows pending data to drain
before EOF. DDOS supports 16 pipe pairs with 4 KiB per direction.

The local `AF_UNIX` stream adapter routes `socket`, `bind`, `listen`,
`connect`, and `accept` to the same pipe queues. Nano-X can use its
existing local socket protocol. Named pipes and sockets expose distinct
interfaces, similar to the split in Wine.
[Wine named pipes](https://github.com/wine-mirror/wine/blob/master/server/named_pipe.c) and
[Wine sockets](https://github.com/wine-mirror/wine/blob/master/server/sock.c).

### BusyBox, runit, and Nano-X

Place the upstream source trees in the ignored `.ref/musl`,
`.ref/busybox`, `.ref/runit`, `.ref/microwindows`, and
`.ref/freetype` directories. Use
[upstream musl](https://git.musl-libc.org/git/musl/),
[BusyBox 1.36.1](https://busybox.net/),
[runit](https://github.com/g-pape/runit), and
[Microwindows](https://github.com/ghaerr/microwindows), and
[FreeType](https://github.com/freetype/freetype). Use
a POSIX build shell, GNU Make, a host C compiler, and LLVM on PATH. The
i686 musl sysroot also needs Linux UAPI headers and matching GCC runtime
objects for Clang's static links. The scripts work with native Windows
LLVM and an MSYS shell and host tools.

```sh
sh tools/build-musl.sh
sh tools/build-busybox.sh
sh tools/build-runit.sh
git clone --depth 1 --branch VER-2-14-3 https://github.com/freetype/freetype.git .ref/freetype
sh tools/build-microwindows.sh
python build.py
```

The builder installs the available static ELF32 outputs as regular files
in `B:/bin`. Use `--busybox FILE`, `--runit DIR`, or `--microwindows DIR`
to use explicitly supplied binaries. BusyBox provides `busybox` and
`sh` with the [Hush and applet configuration](tools/busybox.config).
The runit tools are `runit`, `runit-init`, `runsvdir`, `runsv`, and `sv`.
The graphics build installs `nano-X`, `nanowm`, `taskbar`, `calculator`,
`settings`, `task_mgr`, `file_manager`, `notepad`, and `snake`.

The native loader also accepts scripts whose shebang names an absolute
ELF interpreter path. The image includes executable runit stages in
`B:/etc/runit`, the login shell service in `B:/etc/service/shell/run`,
and a login profile that checks for `/dev/fb0`.

Boot currently goes directly to the DDOS prompt. Runit remains installed
in B: for development. PID 1 supervision needs writable service state,
terminal control, and `getcwd`. Use `NANOWM`
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

A zero-byte write sets the file length to the current position.
`AH=40h` can write ext2 files through single and double indirect blocks,
and writes to console handles `1` and `2`. File rename,
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
mapping for a page-aligned GOP framebuffer of up to 8 MiB. Nano-X draws
into a RAM shadow and asks the kernel to present the changed rectangle
after app paint transactions complete. The taskbar repaints the clock when the minute
changes. GOP presentation uses a software copy and has no hardware
page flip or vertical synchronization. The UEFI
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
| `src/drivers` | Block devices, ATA PIO, EHCI USB storage, PS/2, and the console bitmap font |
| `src/fs` | VFS drive routing, FAT12, and ext2 |
| `src/kernel` | Shell, console helpers, DOS executable loader and DOS file handles |
| `user` | Native startup code, C library, linker script, demo, and Nano-X desktop apps |
| `src/include`, `tools` | Shared interfaces and the host-side ext2 formatter |

## QEMU smoke checks

Create a fresh USB image with the commands in [USB 2.0 storage](#usb-20-storage).
Boot it with `--usb-image` on either BIOS or UEFI. Run these commands
at the DDOS prompt.

```text
mkdir E:/smoke
write E:/smoke/check.txt DDOS storage check
ren E:/smoke/check.txt E:/smoke/renamed.txt
ls E:/smoke
type E:/smoke/renamed.txt
del E:/smoke/renamed.txt
rmdir E:/smoke
ls E:/
```

The first listing should show `renamed.txt`. `TYPE` should print
`DDOS storage check`. The final listing should show an empty drive.

On UEFI, enter `NANOWM`, open **File Manager** from the DDOS menu, and
select **E:** to check that the desktop can browse the same disk. To
check copying, create a file on the USB image, select it, choose Copy,
open a writable destination on **D:**, and choose Paste. Use a new
filename so the destination is available.

## Component provenance

| Component | Origin and DDOS changes |
| --- | --- |
| DOS shell, COM/MZ loader, and INT 21h file calls | DDOS C/NASM implementation of a subset of DOS conventions, using the published [Microsoft MS-DOS 4.0 source](https://github.com/microsoft/MS-DOS/tree/main/v4.0) as a local reference. The drive-letter paths, DOS command names, PSP, and interrupt conventions come from DOS. |
| BIOS boot sector and UEFI boot application | DDOS boot code. BIOS loads the flat kernel from reserved FAT12 sectors. The UEFI application uses the firmware GOP interface, then hands off to the 32-bit kernel. [UEFI specifications](https://uefi.org/specifications). |
| Kernel, processes, VFS, and system image builder | DDOS implementations in C, NASM, and Python. FAT12, ATA PIO, ext2, and static ELF32 use their underlying formats and interfaces. DDOS combines drive letters with POSIX absolute paths. |
| Block layer and USB 2.0 storage | DDOS sector interface and DOS-style drive routing. The EHCI controller and USB mass-storage implementation were written for DDOS with FreeBSD's [EHCI driver](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/controller/ehci.c) and [umass driver](https://cgit.freebsd.org/src/tree/sys/dev/usb/storage/umass.c) as references. The interfaces follow Intel's [EHCI specification](https://www.intel.com/content/www/us/en/products/docs/io/universal-serial-bus/ehci-specification.html) and USB-IF's [Bulk-Only Transport specification](https://www.usb.org/document-library/mass-storage-bulk-only-10). |
| PS/2, framebuffer, and guest power | DDOS drivers and QEMU integration. The PS/2 packet handling was informed by [Linux psmouse](https://github.com/torvalds/linux/blob/master/drivers/input/mouse/psmouse-base.c) and [FreeBSD psm](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/atkbdc/psm.c). The QEMU launcher uses [firmware configuration](https://www.qemu.org/docs/master/specs/fw_cfg.html), and guest shutdown uses [QEMU's PIIX4 ACPI controller](https://github.com/qemu/qemu/blob/master/hw/acpi/piix4.c). |
| Native ABI, pipes, and local sockets | DDOS runtime with a subset of the Linux i386 syscall ABI for musl. The named-pipe namespace follows Windows naming and the interface split was informed by [Wine](https://github.com/wine-mirror/wine/blob/master/server/named_pipe.c). DDOS implements its own queues and local socket adapter. |
| C libraries | The small freestanding library in `user/` is DDOS code. The optional static libc is [upstream musl](https://git.musl-libc.org/git/musl/) with a local i386 TLS selector adjustment for Ring 2. |
| BusyBox and runit | Upstream [BusyBox](https://busybox.net/) supplies Hush and its configured applets. Upstream [runit](https://github.com/g-pape/runit) is cross-built by DDOS. The applet configuration, stage scripts, shell service, and image integration are local. |
| Frame presentation | DDOS shadow-buffer and changed-rectangle copy for GOP, guided by [Linux DRM damage handling](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/drm_plane.c) and [FreeBSD's EFI framebuffer console](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/vt/hw/efifb/efifb.c). |
| Graphics server and window manager | [Microwindows](https://github.com/ghaerr/microwindows) supplies Nano-X and the floating NanoWM. DDOS provides its input adapters, build configuration, desktop startup, and blue window colors. |
| App menu, taskbar, Calculator, Settings, Task Manager, File Manager, Notepad, Snake, and icons | Original DDOS desktop components. File Manager uses familiar Explorer-style interactions, Notepad follows plain-text editor conventions, and Snake uses the classic grid-based game rules. Their blue tonal palette uses [Material 3 color roles](https://m3.material.io/styles/color/the-color-system). The dark tint over a blurred screen snapshot adapts the [UX Pilot glassmorphism guide](https://uxpilot.ai/blogs/glassmorphism-ui) and [Apple HIG materials guidance](https://developer.apple.com/design/human-interface-guidelines/materials) to Nano-X drawing. |
| Desktop typeface and font loading | The bundled [Inter](https://github.com/rsms/inter) Regular is an ASCII subset installed as `B:/fonts/Inter.woff` under the [SIL Open Font License](system/fonts/Inter-OFL.txt). [FreeType](https://freetype.org/) provides OpenType and WOFF decoding through Nano-X under the [FreeType License](system/fonts/FreeType-FTL.txt). |
| Quick launcher | Original DDOS taskbar launcher inspired by [Rofi](https://github.com/davatorium/rofi), using Nano-X and native app launching. |
| Console typeface | Public-domain [font8x8](https://github.com/dhepper/font8x8), included in the graphics console. |
| Toolchain and emulation | Python build and boot wrappers are DDOS code. Clang, LLD, NASM, QEMU, and EDK II/OVMF are external tools or firmware. |

The repository's [MIT license](LICENSE) covers the DDOS project files as
described there. Upstream projects retain their own licenses. Reference
checkouts in `.ref/` and generated binaries and images in `build/` are
excluded from Git.
