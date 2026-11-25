"""Format DDOS's small, single-group data volume as standard ext2."""

from pathlib import Path
from struct import pack_into
from time import time
from uuid import uuid4

BLOCK = 1024
BLOCKS = 4096
INODES = 128
ROOT_BLOCK = 5 + INODES * 128 // BLOCK


def mark(bitmap, start, end):
    for index in range(start, end):
        bitmap[index // 8] |= 1 << (index % 8)


def image():
    disk = bytearray(BLOCK * BLOCKS)
    free_blocks = BLOCKS - ROOT_BLOCK - 1
    free_inodes = INODES - 10
    now = int(time())

    superblock = memoryview(disk)[BLOCK:2 * BLOCK]
    pack_into("<13I", superblock, 0,
              INODES, BLOCKS, 0, free_blocks, free_inodes, 1, 0, 0,
              BLOCK * 8, BLOCK * 8, INODES, 0, now)
    pack_into("<6H", superblock, 52, 0, 0xFFFF, 0xEF53, 1, 1, 0)
    pack_into("<7I", superblock, 64, now, 0, 0, 1, 0, 11, 128)
    pack_into("<III", superblock, 92, 0, 2, 0)
    superblock[104:120] = uuid4().bytes
    superblock[120:129] = b"DDOS_DATA"

    pack_into("<IIIHHH", disk, 2 * BLOCK, 3, 4, 5, free_blocks, free_inodes, 1)
    blocks = memoryview(disk)[3 * BLOCK:4 * BLOCK]
    inodes = memoryview(disk)[4 * BLOCK:5 * BLOCK]
    mark(blocks, 0, ROOT_BLOCK)
    mark(blocks, BLOCKS - 1, BLOCK * 8)
    mark(inodes, 0, 10)
    mark(inodes, INODES, BLOCK * 8)

    root = 5 * BLOCK + 128
    pack_into("<HHIIIIIHHI", disk, root, 0o40755, 0, BLOCK, now, now, now, 0, 0, 2, 2)
    pack_into("<I", disk, root + 40, ROOT_BLOCK)
    pack_into("<IHBB1s", disk, ROOT_BLOCK * BLOCK, 2, 12, 1, 2, b".")
    pack_into("<IHBB2s", disk, ROOT_BLOCK * BLOCK + 12, 2, BLOCK - 12, 2, 2, b"..")
    return disk


def create_if_missing(path: Path):
    if path.exists():
        return False
    data = image()
    try:
        with path.open("xb") as output:
            output.write(data)
    except FileExistsError:
        return False
    return True