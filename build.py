"""Build DDOS for BIOS and x64 UEFI with native LLVM and NASM tools."""

import argparse
from pathlib import Path
import shutil
import struct
import subprocess

from tools.mkext2 import create_if_missing
from tools.system_image import build_system

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
C_FLAGS = (
    "-std=c11", "-Os", "-ffreestanding", "-fno-stack-protector",
    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-Wall", "-Wextra", "-Werror",
)
KERNEL_FLAGS = ("--target=i386-none-elf", "-m32", "-march=i386", "-fno-pic", "-Isrc/include", *C_FLAGS)


def run(*command):
    tool = shutil.which(command[0])
    if tool is None:
        raise SystemExit(f"Required tool not found on PATH: {command[0]}")
    subprocess.run((tool, *command[1:]), cwd=ROOT, check=True)


def make_boot_image(boot, kernel):
    size, _, reserved, copies, _, total, media, fat_size = (
        struct.unpack_from("<HBHBHHBH", boot, 11)
    )
    if len(boot) != size or len(kernel) > (reserved - 1) * size:
        raise ValueError("kernel does not fit in the boot loader's reserved sectors")
    image = bytearray(total * size)
    image[:size] = boot
    image[size:size + len(kernel)] = kernel
    for copy in range(copies):
        offset = (reserved + copy * fat_size) * size
        image[offset:offset + 3] = bytes((media, 0xFF, 0xFF))
    return image


KERNEL_COMPONENTS = {
    "power": "src/arch/x86/power.c",
    "ehci": "src/drivers/ehci.c",
    "usb-storage": "src/drivers/usb-storage.c",
    "block": "src/drivers/block.c",
    "ata": "src/drivers/ata.c",
    "ext2-read": "src/fs/ext2/read.c",
    "ext2-write": "src/fs/ext2/write.c",
    "fat12": "src/fs/fat12.c",
    "vfs": "src/fs/vfs.c",
    "console": "src/kernel/console.c",
    "domains": "src/arch/x86/domains.c",
    "dos": "src/kernel/dos.c",
    "native": "src/kernel/native.c",
    "native-elf": "src/kernel/native_elf.c",
    "native-clock": "src/kernel/native_clock.c",
    "shell": "src/kernel/shell.c",
}


def build_kernel_components():
    objects = ["build/protected.o"]
    run("nasm", "-f", "elf32", "src/arch/x86/protected.asm", "-o", objects[0])
    domain_entry = "build/domain-entry.o"
    run("nasm", "-f", "elf32", "src/arch/x86/domain_entry.asm", "-o", domain_entry)
    objects.append(domain_entry)
    for name, source in KERNEL_COMPONENTS.items():
        target = f"build/{name}.o"
        run("clang", *KERNEL_FLAGS, "-Oz", "-c", source, "-o", target)
        objects.append(target)
    return objects


def build_uefi(objects):
    esp = BUILD / "uefi" / "EFI" / "BOOT"
    esp.mkdir(parents=True, exist_ok=True)
    run("nasm", "-f", "elf32", "src/boot/uefi/entry.asm", "-o", "build/uefi-entry.o")
    run("clang", *KERNEL_FLAGS, "-Isrc/drivers/video", "-c", "src/boot/uefi/platform.c", "-o", "build/uefi-platform.o")
    run("clang", *KERNEL_FLAGS, "-Oz", "-c", "src/drivers/ps2.c", "-o", "build/ps2.o")
    run("ld.lld", "-m", "elf_i386", "-T", "src/arch/x86/kernel.ld", "build/uefi-entry.o", "build/ps2.o",
        "build/uefi-platform.o", *objects,
        "-o", "build/uefi-kernel.elf")
    run("llvm-objcopy", "-O", "binary", "build/uefi-kernel.elf", "build/uefi-kernel.bin")
    run("nasm", "-f", "win64", "src/boot/uefi/payload.asm", "-o", "build/uefi-payload.obj")
    run("clang", "--target=x86_64-pc-windows-msvc", "-mno-red-zone", *C_FLAGS,
        "-Isrc/include", "-c", "src/boot/uefi/loader.c", "-o", "build/uefi-loader.obj")
    run("lld-link", "/machine:x64", "/subsystem:efi_application", "/entry:efi_main",
        "/nodefaultlib", "/out:build/uefi/EFI/BOOT/BOOTX64.EFI",
        "build/uefi-loader.obj", "build/uefi-payload.obj")
    print("Built build/uefi/EFI/BOOT/BOOTX64.EFI")


def build_native_app(source):
    run("nasm", "-f", "elf32", "user/crt.asm", "-o", "build/user-crt.o")
    for name, path in (("libc", "user/libc.c"), ("app", source)):
        run("clang", *KERNEL_FLAGS, "-Iuser/include", "-ffunction-sections",
            "-fdata-sections", "-c", path, "-o", f"build/user-{name}.o")
    run("ld.lld", "-m", "elf_i386", "--gc-sections", "--strip-all", "-T", "user/link.ld",
        "build/user-crt.o", "build/user-libc.o", "build/user-app.o", "-o", "build/DDOSAPP.ELF")
    return BUILD / "DDOSAPP.ELF"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", action="append", type=Path, default=[],
                        help="install a file in B:/bin (repeatable)")
    parser.add_argument("--app", type=Path, default=ROOT / "user/app.c",
                        help="build a Ring 2 C application in place of the default demo")
    parser.add_argument("--busybox", type=Path,
                        help="install an i386 static BusyBox as B:/bin/busybox and B:/bin/sh")
    parser.add_argument("--runit", type=Path, help="directory containing static runit tools")
    parser.add_argument("--microwindows", type=Path, help="directory containing static Nano-X desktop programs")
    parser.add_argument("--desktop", action="store_true",
                        help="require a NanoWM desktop in the system image")
    parser.add_argument("--system-image", type=Path, default=BUILD / "system.ext2",
                        help="output path for the ext2 system image")
    args = parser.parse_args()
    if args.busybox is None and (BUILD / "busybox").is_file():
        args.busybox = BUILD / "busybox"
    if args.runit is None and all((BUILD / "runit" / name).is_file()
                                  for name in ("runit", "runit-init", "runsvdir", "runsv", "sv")):
        args.runit = BUILD / "runit"
    if args.microwindows is None and (BUILD / "microwindows" / "nanowm").is_file():
        args.microwindows = BUILD / "microwindows"
    if args.desktop and (args.microwindows is None or
                         any(not (args.microwindows / name).is_file()
                             for name in ("nano-X", "nanowm", "taskbar", "calculator", "settings", "task_mgr", "file_manager"))):
        parser.error("Desktop binaries are missing. Run sh tools/build-microwindows.sh or pass --microwindows.")
    if args.system_image.resolve() in (BUILD / "data.ext2", BUILD / "ddos.img"):
        parser.error("--system-image must be separate from the persistent data and boot images")
    BUILD.mkdir(exist_ok=True)
    args.system_image.parent.mkdir(parents=True, exist_ok=True)
    native_app = build_native_app(args.app.resolve())
    run("nasm", "-f", "bin", "src/boot/bios/sector.asm", "-o", "build/boot.bin")
    run("nasm", "-f", "elf32", "src/boot/bios/platform.asm", "-o", "build/platform.o")
    objects = build_kernel_components()
    run("clang", *KERNEL_FLAGS, "-Oz", "-c", "src/kernel/dos_files.c", "-o", "build/dos-files.o")
    run("ld.lld", "-m", "elf_i386", "-T", "src/arch/x86/kernel.ld",
        "build/platform.o", "build/dos-files.o", *objects, "-o", "build/kernel.elf")
    run("llvm-objcopy", "-O", "binary", "build/kernel.elf", "build/kernel.bin")
    try:
        image = make_boot_image((BUILD / "boot.bin").read_bytes(),
                                (BUILD / "kernel.bin").read_bytes())
        build_system(args.system_image, native_app, args.file, args.busybox, args.runit, args.microwindows)
    except PermissionError as error:
        if getattr(error, "winerror", None) in (5, 32):
            parser.error(f"Windows could not replace {args.system_image}. "
                         "Close the VM using it or pass --system-image with a different path.")
        raise
    except (OSError, ValueError) as error:
        parser.error(str(error))
    (BUILD / "ddos.img").write_bytes(image)
    print(f"Built build/ddos.img and {args.system_image}")
    build_uefi(objects)
    if create_if_missing(BUILD / "data.ext2"):
        print("Created build/data.ext2")


if __name__ == "__main__":
    main()
