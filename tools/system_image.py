"""Build the read-only DDOS ext2 system volume from host files."""

from pathlib import Path
from struct import pack_into, unpack_from
from tempfile import NamedTemporaryFile
from time import time

from tools.mkext2 import BLOCK, BLOCKS, INODES, ROOT_BLOCK, image, mark


class SystemImage:
    def __init__(self):
        self.disk = image()
        self.disk[BLOCK + 120:BLOCK + 136] = b"DDOS_SYSTEM".ljust(16, b"\0")
        self.next_inode = 11
        self.next_block = ROOT_BLOCK + 1
        self.directories = 1
        self.time = int(time())

    def block(self, data):
        if self.next_block >= BLOCKS or len(data) > BLOCK:
            raise ValueError("system.ext2 is full")
        number = self.next_block
        self.next_block += 1
        offset = number * BLOCK
        self.disk[offset:offset + len(data)] = data
        return number

    def pointers(self, values):
        table = bytearray(BLOCK)
        pack_into(f"<{len(values)}I", table, 0, *values)
        return self.block(table)

    def content(self, data):
        blocks = [self.block(data[i:i + BLOCK]) for i in range(0, len(data), BLOCK)]
        direct = blocks[:12] + [0] * (12 - min(12, len(blocks)))
        fanout = BLOCK // 4
        single = self.pointers(blocks[12:12 + fanout]) if len(blocks) > 12 else 0
        tail = blocks[12 + fanout:]
        double = 0
        if tail:
            double = self.pointers([
                self.pointers(tail[i:i + fanout]) for i in range(0, len(tail), fanout)
            ])
        return direct + [single, double, 0]

    def reserve_inode(self):
        if self.next_inode > INODES:
            raise ValueError("system.ext2 has no free inodes")
        number = self.next_inode
        self.next_inode += 1
        return number

    def inode(self, number, mode, size, links, blocks, pointers):
        offset = 5 * BLOCK + (number - 1) * 128
        pack_into("<HHIIIIIHHI", self.disk, offset, mode, 0, size,
                  self.time, self.time, self.time, 0, 0, links, blocks * 2)
        pack_into("<15I", self.disk, offset + 40, *pointers)

    def file(self, data):
        number = self.reserve_inode()
        start = self.next_block
        pointers = self.content(data)
        self.inode(number, 0o100755, len(data), 1, self.next_block - start, pointers)
        return number

    def directory(self, number, parent, entries):
        items = [(".", number, 2), ("..", parent, 2), *entries]
        data = bytearray(BLOCK)
        offset = 0
        for index, (name, inode, kind) in enumerate(items):
            name = name.encode("ascii")
            minimum = (8 + len(name) + 3) & ~3
            size = BLOCK - offset if index == len(items) - 1 else minimum
            if size < minimum or offset + size > BLOCK:
                raise ValueError("system.ext2 directory is full")
            pack_into("<IHBB", data, offset, inode, size, len(name), kind)
            data[offset + 8:offset + 8 + len(name)] = name
            offset += size
        if number == 2:
            block = ROOT_BLOCK
            self.disk[block * BLOCK:(block + 1) * BLOCK] = data
        else:
            block = self.block(data)
            self.directories += 1
        self.inode(number, 0o40755, BLOCK, 2 + sum(kind == 2 for _, _, kind in entries),
                   1, [block] + [0] * 14)

    def finish(self):
        blocks = memoryview(self.disk)[3 * BLOCK:4 * BLOCK]
        inodes = memoryview(self.disk)[4 * BLOCK:5 * BLOCK]
        mark(blocks, ROOT_BLOCK, self.next_block - 1)
        mark(inodes, 10, self.next_inode - 1)
        free_blocks = BLOCKS - self.next_block
        free_inodes = INODES - self.next_inode + 1
        pack_into("<II", self.disk, BLOCK + 12, free_blocks, free_inodes)
        pack_into("<HHH", self.disk, 2 * BLOCK + 12, free_blocks, free_inodes,
                  self.directories)
        return self.disk


def build_system(output, app, files, busybox):
    fs = SystemImage()
    binary, configs, temporary = (fs.reserve_inode() for _ in range(3))
    programs = {"ddosapp": app.read_bytes()}
    for path in files:
        name = path.name
        if not name.isascii() or len(name) > 80 or name in programs or name in (".", ".."):
            raise ValueError(f"invalid or duplicate system filename {name}")
        programs[name] = path.read_bytes()
    if busybox:
        image_bytes = busybox.read_bytes()
        if len(image_bytes) < 52 or image_bytes[:6] != b"\x7fELF\x01\x01":
            raise ValueError("BusyBox must be a little-endian i386 ELF32 executable")
        kind, machine = unpack_from("<HH", image_bytes, 16)
        offset = unpack_from("<I", image_bytes, 28)[0]
        width, count = unpack_from("<HH", image_bytes, 42)
        if (kind, machine, width) != (2, 3, 32) or not count or offset + count * width > len(image_bytes):
            raise ValueError("BusyBox needs a valid static i386 ELF executable header")
        if any(unpack_from("<I", image_bytes, offset + i * width)[0] in (2, 3)
               for i in range(count)):
            raise ValueError("BusyBox must be statically linked")
        if programs.keys() & {"busybox", "sh"}:
            raise ValueError("BusyBox installation conflicts with another system file")
        programs.update(busybox=image_bytes, sh=image_bytes)
    fs.directory(binary, 2, [(name, fs.file(data), 1) for name, data in programs.items()])
    fs.directory(configs, 2, [])
    fs.directory(temporary, 2, [])
    fs.directory(2, 2, [("bin", binary, 2), ("etc", configs, 2), ("tmp", temporary, 2)])
    result = fs.finish()
    with NamedTemporaryFile(dir=output.parent, prefix="system-", delete=False) as temporary:
        path = Path(temporary.name)
        try:
            temporary.write(result)
            temporary.close()
            path.replace(output)
        finally:
            temporary.close()
            path.unlink(missing_ok=True)