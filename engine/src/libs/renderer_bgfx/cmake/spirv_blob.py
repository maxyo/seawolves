"""Reading the SPIR-V module of a bgfx shader blob (shaderc output, `<name>.spv.bin`), for the build's and the host
checks' scripts: the module's words, its instructions and its debug names."""

import struct

MAGIC = 0x07230203
OP_NAME = 5


def module(blob):
    """The SPIR-V words of a bgfx shader binary: the module follows its byte size (uint32)."""
    start = blob.find(struct.pack('<I', MAGIC))
    if start < 4:
        raise ValueError('no SPIR-V module in the blob')
    size = struct.unpack_from('<I', blob, start - 4)[0]
    return struct.unpack_from(f'<{size // 4}I', blob, start)


def instructions(words):
    """(opcode, operand words) of every instruction after the header."""
    at = 5  # magic, version, generator, bound, schema
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 0xFFFF
        if count == 0 or at + count > len(words):
            raise ValueError(f'malformed SPIR-V at word {at}')
        yield op, words[at + 1 : at + count]
        at += count


def string(operands):
    """A literal string operand (UTF-8, NUL-terminated, padded to words)."""
    return struct.pack(f'<{len(operands)}I', *operands).split(b'\0', 1)[0].decode()


def names(words):
    """{id: OpName} of a module."""
    return {args[0]: string(args[1:]) for op, args in instructions(words) if op == OP_NAME}
