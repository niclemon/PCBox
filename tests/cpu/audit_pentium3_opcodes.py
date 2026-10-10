#!/usr/bin/env python3
"""Fail if a Pentium III dispatch entry has no timing probe in the inventory.

The C catalogue is ISA-based and does not derive coverage from timing tables.
This second, independent check reconciles it with *both* production x87 engines
and all four integer decoder size contexts. It checks dispatch coverage, not
whether an opcode handler implements the right architectural semantics.
"""

import argparse
import re
import subprocess
from pathlib import Path


def read_table(path, name):
    text = path.read_text(encoding="utf-8")
    match = re.search(r"const OpFn OP_TABLE\(" + re.escape(name)
                      + r"\)\[(\d+)\]\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ValueError(f"Cannot find dispatch table {name} in {path}")
    body = re.sub(r"/\*.*?\*/", "", match[2], flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    entries = [entry.strip() for entry in body.split(",") if entry.strip()]
    if len(entries) != int(match[1]) or any(not re.fullmatch(r"\w+", e) for e in entries):
        raise ValueError(f"Unrecognized initializer for {name}; update the audit parser")
    return entries


def inventory(executable):
    listing = subprocess.run([str(executable), "--list"], check=True,
                             capture_output=True, text=True).stdout
    coverage = {}
    for line in listing.splitlines():
        family, map_name, opcode, form, mask_value, mode, name = line.split("/")
        mask, value = (int(v, 16) for v in mask_value.split("-"))
        key = (map_name, int(opcode, 16), mode)
        slots = coverage.setdefault(key, set())
        for modrm in range(256):
            if modrm & mask != value:
                continue
            if form == "reg" and modrm < 0xC0 or form == "mem" and modrm >= 0xC0:
                continue
            if modrm in slots:
                raise ValueError(f"Overlapping inventory encoding: {line}, {modrm:02x}")
            slots.add(modrm)
    return coverage


# These entries are deliberately outside the architectural PIII inventory.
# Require the exact handler too, so reassignment cannot silently bypass coverage.
EXTENSIONS = {
    0x0D: ("opNOP", "reserved on Intel PIII; emulator accepts an AMD prefetch slot as NOP"),
    0x24: ("opMOV_r_TRx_a", "test registers are not supported on PIII"),
    0x26: ("opMOV_TRx_r_a", "test registers are not supported on PIII"),
    0x3F: ("opVPCEXT", "emulator Virtual PC extension"),
}
PREFIXES = {0x0F, 0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67,
            *range(0xD8, 0xE0), 0xF0, 0xF2, 0xF3}
EMPTY = {"ILLEGAL", "ILLEGAL_a16", "ILLEGAL_a32", "NULL"}


def audit(root, executable):
    covered = inventory(executable)
    ops_file = root / "src/cpu/386_ops.h"
    base = read_table(ops_file, "386")
    extended = read_table(ops_file, "pentium3_0f")
    scalar = read_table(ops_file, "pentium3_REPE_0f")
    errors = []
    checked = 0
    for context in range(4):
        mode = f"o{32 if context & 1 else 16}-a{32 if context & 2 else 16}"
        for opcode in range(256):
            index = context * 256 + opcode
            for map_name, table in (("base", base), ("0f", extended), ("f3-0f", scalar)):
                handler = table[index]
                if handler in EMPTY or map_name == "base" and opcode in PREFIXES:
                    continue
                if map_name == "f3-0f":
                    # F3 aliases of ordinary opcodes are not new instructions.
                    normal = lambda h: re.sub(r"_a(?:16|32)$", "_a", h)
                    if normal(handler) == normal(extended[index]):
                        continue
                if map_name == "0f" and opcode in EXTENSIONS:
                    expected, _ = EXTENSIONS[opcode]
                    if handler == expected or handler == expected + ("32" if context & 2 else "16"):
                        continue
                checked += 1
                if not covered.get((map_name, opcode, mode)):
                    errors.append(f"Unprobed {map_name} {opcode:02x} {mode}: {handler}")

        # x87 has a distinct ModR/M dispatch, including P6 FCMOV/FCOMI tables.
        # The compact D8/DC tables are indexed by ModR/M >> 3; the rest by byte.
        address_size = 32 if context & 2 else 16
        for soft in (False, True):
            for opcode in range(0xD8, 0xE0):
                family = "686_" if opcode in (0xDA, 0xDB, 0xDF) else ""
                name = f"{'sf_' if soft else ''}fpu_{family}{opcode:02x}_a{address_size}"
                table = read_table(root / "src/cpu/x87_ops.h", name)
                slots = covered.get(("x87", opcode, mode), set())
                for modrm in range(256):
                    handler = table[modrm >> 3 if len(table) == 32 else modrm]
                    if handler in EMPTY:
                        continue
                    checked += 1
                    if modrm not in slots:
                        errors.append(f"Unprobed x87 {opcode:02x} {modrm:02x} {mode}: {name}/{handler}")

    if errors:
        raise ValueError("\n".join(errors[:40]) + (f"\n... {len(errors)} total gaps" if len(errors) > 40 else ""))
    print(f"Pentium III dispatch coverage passed: {checked} implemented entries checked; "
          "both x87 engines, all operand/address sizes.")
    for opcode, (_, reason) in EXTENSIONS.items():
        print(f"  Excluded 0f {opcode:02x}: {reason}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    try:
        audit(args.source_root, args.executable.resolve())
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Pentium III opcode coverage FAILED:\n{exc}\n")


if __name__ == "__main__":
    main()
