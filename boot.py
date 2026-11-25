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
    args = parser.parse_args()

    if not (ROOT / (EFI if args.uefi else IMAGE)).is_file():
        raise SystemExit("DDOS image is missing. Run python build.py first.")
    qemu = find_qemu(args.uefi)
    if qemu is None:
        raise SystemExit("QEMU was not found. Install QEMU and add it to PATH.")

    data_disk = ()
    if (ROOT / DATA).is_file():
        data_disk = ("-drive", f"file={DATA.as_posix()},format=raw,if=ide,index=1")
    if (ROOT / SYSTEM).is_file():
        data_disk += ("-drive", f"file={SYSTEM.as_posix()},format=raw,if=ide,index=2")
    try:
        if args.uefi:
            code, variables = find_firmware(qemu)
            code_path = code.as_posix().replace(",", ",,")
            with tempfile.TemporaryDirectory(prefix="ddos-qemu-") as scratch:
                scratch = Path(scratch)
                vars_path = Path(shutil.copyfile(variables, scratch / "vars.fd"))
                esp = scratch / "esp"
                shutil.copytree(ROOT / "build/uefi", esp)
                vars_option = vars_path.as_posix().replace(",", ",,")
                esp_option = esp.as_posix().replace(",", ",,")
                command = (
                    qemu,
                    "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code_path}",
                    "-drive", f"if=pflash,unit=1,format=raw,file={vars_option}",
                    "-drive", f"file=fat:rw:{esp_option},format=raw,if=ide,index=0", *data_disk,
                )
                return subprocess.run(command, cwd=ROOT).returncode
        command = (qemu, "-drive", f"file={IMAGE.as_posix()},format=raw,if=floppy",
                   "-boot", "a", *data_disk)
        return subprocess.run(command, cwd=ROOT).returncode
    except OSError as error:
        raise SystemExit(f"Could not launch QEMU: {error}") from error


if __name__ == "__main__":
    raise SystemExit(main())