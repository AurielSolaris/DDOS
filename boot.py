"""Find QEMU and boot DDOS."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
IMAGE = Path("build/ddos.img")
DATA = Path("build/data.ext2")
SYSTEM = Path("build/system.ext2")
EFI = Path("build/uefi/EFI/BOOT/BOOTX64.EFI")
EMULATORS = ("qemu-system-i386", "qemu-system-x86_64")


def find_qemu(uefi=False):
    names = EMULATORS[1:] if uefi else EMULATORS
    for name in names:
        if executable := shutil.which(name):
            return Path(executable).resolve()

    if os.name == "nt":
        for variable in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"):
            directory = os.environ.get(variable)
            if not directory:
                continue
            for name in names:
                executable = Path(directory) / "qemu" / f"{name}.exe"
                if executable.is_file():
                    return executable

    return None


def find_firmware(qemu):
    bundled = qemu.parent / "share"
    candidates = (
        (bundled / "edk2-x86_64-code.fd", bundled / "edk2-i386-vars.fd"),
        (Path("/usr/share/edk2/x64/OVMF_CODE.fd"), Path("/usr/share/edk2/x64/OVMF_VARS.fd")),
        (Path("/usr/share/OVMF/OVMF_CODE_4M.fd"), Path("/usr/share/OVMF/OVMF_VARS_4M.fd")),
        (Path("/usr/share/OVMF/OVMF_CODE.fd"), Path("/usr/share/OVMF/OVMF_VARS.fd")),
    )
    for code, variables in candidates:
        if code.is_file() and variables.is_file():
            return code, variables
    raise SystemExit("x64 UEFI firmware was not found. Install QEMU's EDK II or OVMF firmware.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uefi", action="store_true", help="boot with x64 UEFI and GOP")
    parser.add_argument("--calculator", action="store_true", help="boot UEFI and open the calculator")
    parser.add_argument("--debug-log", type=Path, help="write guest debug console output to a file")
    parser.add_argument("--system-image", type=Path, default=SYSTEM, help="ext2 system image to boot")
    parser.add_argument("--usb-image", type=Path, help="attach a raw image as a USB 2.0 storage device")
    args = parser.parse_args()
    args.uefi |= args.calculator
    startup = ("-fw_cfg", "name=opt/ddos/calculator,string=1") if args.calculator else ()
    debug = ()
    if args.debug_log:
        debug = ("-debugcon", f"file:{args.debug_log.resolve().as_posix()}")

    if not (ROOT / (EFI if args.uefi else IMAGE)).is_file():
        raise SystemExit("DDOS image is missing. Run python build.py first.")
    qemu = find_qemu(args.uefi)
    if qemu is None:
        raise SystemExit("QEMU was not found. Install QEMU and add it to PATH.")

    data_disk = ()
    if (ROOT / DATA).is_file():
        data_disk = ("-drive", f"file={DATA.as_posix()},format=raw,if=ide,index=1")
    usb = ()
    if args.usb_image:
        image = args.usb_image.resolve()
        if not image.is_file():
            parser.error(f"USB disk image was not found: {image}")
        option = image.as_posix().replace(",", ",,")
        usb = ("-device", "usb-ehci,id=ddos_usb", "-drive",
               f"file={option},format=raw,if=none,id=ddos_usb_disk",
               "-device", "usb-storage,bus=ddos_usb.0,drive=ddos_usb_disk")
    try:
        with tempfile.TemporaryDirectory(prefix="ddos-qemu-") as scratch:
            scratch = Path(scratch)
            system_image = ROOT / args.system_image
            if system_image.is_file():
                # Keep QEMU from locking the rebuildable system image on Windows.
                system_copy = scratch / "system.ext2"
                shutil.copyfile(system_image, system_copy)
                system_option = system_copy.as_posix().replace(",", ",,")
                data_disk += ("-drive", f"file={system_option},format=raw,if=ide,index=2")
            elif args.calculator or args.system_image != SYSTEM:
                raise SystemExit(f"DDOS system image was not found: {system_image}")
            if args.uefi:
                code, variables = find_firmware(qemu)
                code_path = code.as_posix().replace(",", ",,")
                vars_path = Path(shutil.copyfile(variables, scratch / "vars.fd"))
                esp = scratch / "esp"
                shutil.copytree(ROOT / "build/uefi", esp)
                vars_option = vars_path.as_posix().replace(",", ",,")
                esp_option = esp.as_posix().replace(",", ",,")
                command = (
                    qemu,
                    "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code_path}",
                    "-drive", f"if=pflash,unit=1,format=raw,file={vars_option}",
                    "-drive", f"file=fat:rw:{esp_option},format=raw,if=ide,index=0", *data_disk, *startup, *debug,
                )
            else:
                boot_image = scratch / "ddos.img"
                shutil.copyfile(ROOT / IMAGE, boot_image)
                boot_option = boot_image.as_posix().replace(",", ",,")
                command = (qemu, "-drive", f"file={boot_option},format=raw,if=floppy",
                           "-boot", "a", *data_disk, *debug)
            return subprocess.run((*command, *usb), cwd=ROOT).returncode
    except OSError as error:
        raise SystemExit(f"Could not launch QEMU: {error}") from error


if __name__ == "__main__":
    raise SystemExit(main())