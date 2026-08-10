#!/usr/bin/env python3
"""Align a target font's private Gujarati glyphs to the canonical PUA codes.

GujaratiShaper emits the PUA codes generated from its canonical source font.
This helper lets another Gujarati font reuse that shaper by matching ligature
outputs through their logical input sequences, while retaining the target
font's own glyph names for rasterization.
"""

import argparse
import json
import re
from pathlib import Path


PUA_START = 0xE000
PUA_END = 0xF8FF
HEX_LIST = re.compile(r"0x[0-9A-Fa-f]+")


def read_rules(path: Path):
    rules = []
    for line in path.read_text().splitlines():
        if "{" not in line or "}" not in line:
            continue
        values = [int(value, 16) for value in HEX_LIST.findall(line)]
        if len(values) >= 3:
            rules.append((tuple(values[:-1]), values[-1]))
    return rules


def is_pua(cp: int) -> bool:
    return PUA_START <= cp <= PUA_END


def semantic_sequences(rules):
    """Resolve each generated PUA code to its logical input sequence."""
    resolved = {}
    for _ in range(len(rules) + 1):
        changed = False
        for inputs, output in rules:
            sequence = []
            for cp in inputs:
                if is_pua(cp):
                    if cp not in resolved:
                        sequence = None
                        break
                    sequence.extend(resolved[cp])
                else:
                    sequence.append(cp)
            if sequence is None or not is_pua(output) or output in resolved:
                continue
            resolved[output] = tuple(sequence)
            changed = True
        if not changed:
            break
    return resolved


def load_mapping(path: Path):
    return {int(cp, 16): glyph for cp, glyph in json.loads(path.read_text()).items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--canonical-header", type=Path, required=True)
    parser.add_argument("--canonical-mapping", type=Path, required=True)
    parser.add_argument("--target-header", type=Path, required=True)
    parser.add_argument("--target-mapping", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    canonical_semantics = semantic_sequences(read_rules(args.canonical_header))
    target_semantics = semantic_sequences(read_rules(args.target_header))
    canonical_mapping = load_mapping(args.canonical_mapping)
    target_mapping = load_mapping(args.target_mapping)

    target_by_semantics = {
        target_semantics[cp]: glyph
        for cp, glyph in target_mapping.items()
        if cp in target_semantics
    }
    aligned = {
        f"0x{canonical_cp:04X}": target_by_semantics[canonical_semantics[canonical_cp]]
        for canonical_cp in sorted(canonical_mapping)
        if canonical_cp in canonical_semantics
        and canonical_semantics[canonical_cp] in target_by_semantics
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(aligned, indent=2) + "\n")
    print(f"Aligned {len(aligned)} of {len(canonical_mapping)} canonical PUA glyphs")


if __name__ == "__main__":
    main()
