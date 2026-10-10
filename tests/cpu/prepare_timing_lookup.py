"""Expose scheduler-selection metadata to the tests without changing production.

The measured opcode callback is included and executed unchanged. This separate
accessor copies only its table-selection statements for inventory/signature
inspection. Fail at build time if the expected function structure changes.
"""
import argparse
from pathlib import Path


def generate(source, output):
    text = source.read_text(encoding="utf-8")
    start = text.index("codegen_timing_p6_opcode(uint8_t opcode,")
    body = text.index("{", start) + 1
    end = text.index("\n    if (ins_table[opcode])", body)
    selection = text[body:end]
    # These variables belong to execution, not table selection.
    lines = selection.splitlines()
    for variable in ("old_last_complete_timestamp", "bit8"):
        matches = [line for line in lines if variable in line]
        if len(matches) != 1 or not matches[0].lstrip().startswith("int "):
            raise ValueError(f"unexpected opcode callback structure: {variable}")
        lines.remove(matches[0])
    selection = "\n".join(lines)
    if "switch (last_prefix)" not in selection or "decode_instruction(" in selection:
        raise ValueError("unexpected opcode table selector")
    generated = (
        "/* Generated metadata accessor; production callback remains unchanged. */\n"
        "static const macro_op_t *\n"
        "codegen_timing_p6_lookup(uint8_t opcode, uint32_t fetchdat, uint64_t *dependency)\n"
        "{" + selection + "\n"
        "    *dependency = ins_table[opcode] ? deps[opcode] : 0;\n"
        "    return ins_table[opcode];\n}\n"
    )
    output.write_text(generated, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.source, args.output)
