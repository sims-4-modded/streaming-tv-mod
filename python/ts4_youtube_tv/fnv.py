# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The FNV-1 hashes that the game uses for the names of resources.

The game makes the instance of a resource key from the FNV-1 64 hash of the name in lower case. The key of a string in
a string table is the FNV-1 32 hash of a name. This module has no import of the game, so the build tool and the tests
can use it.
"""

_FNV64_OFFSET = 0xCBF29CE484222325
_FNV64_PRIME = 0x100000001B3
_FNV64_MASK = 0xFFFFFFFFFFFFFFFF

_FNV32_OFFSET = 0x811C9DC5
_FNV32_PRIME = 0x01000193
_FNV32_MASK = 0xFFFFFFFF


def _ascii_lower(text):
    """Return the text with only the ASCII capital letters changed to small letters."""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in text)


def fnv1_64(name, lower=True):
    """Return the FNV-1 64 hash of the UTF-8 bytes of the name.

    The game hashes the name in lower case. Set lower to False to hash the name as it is.
    """
    if lower:
        name = _ascii_lower(name)
    value = _FNV64_OFFSET
    for byte in name.encode("utf-8"):
        value = (value * _FNV64_PRIME) & _FNV64_MASK
        value ^= byte
    return value


def fnv1_32(name, lower=True):
    """Return the FNV-1 32 hash of the UTF-8 bytes of the name."""
    if lower:
        name = _ascii_lower(name)
    value = _FNV32_OFFSET
    for byte in name.encode("utf-8"):
        value = (value * _FNV32_PRIME) & _FNV32_MASK
        value ^= byte
    return value
