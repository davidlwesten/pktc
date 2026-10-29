"""pktc: schema -> <proto>_gen.h + <proto>_gen.c (+ <proto>_fuzz.c).

    python3 pktc.py SCHEMA [-o OUTDIR] [--fuzz]

OUTDIR defaults to the schema's directory. --fuzz also writes a libFuzzer
harness for the generated code.
"""
import argparse
import pathlib
import sys

from check import SchemaError, check
from emit import emit, emit_fuzz
from ir import lower
from schema import parse


def compile_schema(path: pathlib.Path, fuzz: bool = False) -> tuple[str, dict[str, str]]:
    """Return (proto name, {output file name: text})."""
    s = parse(path.read_text())
    check(s)
    proto = lower(s, path.name)
    h, c = emit(proto)
    files = {f"{proto.name}_gen.h": h, f"{proto.name}_gen.c": c}
    if fuzz:
        files[f"{proto.name}_fuzz.c"] = emit_fuzz(proto)
    return proto.name, files


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate C23 encode/decode code from a schema.")
    ap.add_argument("schema", type=pathlib.Path)
    ap.add_argument("-o", "--outdir", type=pathlib.Path)
    ap.add_argument("--fuzz", action="store_true", help="also write <proto>_fuzz.c")
    args = ap.parse_args()
    try:
        _, files = compile_schema(args.schema, args.fuzz)
    except SchemaError as e:
        for line, msg in e.errors:
            print(f"{args.schema}:{line}: {msg}", file=sys.stderr)
        return 1
    outdir = args.outdir or args.schema.parent
    for name, text in files.items():
        path = outdir / name
        path.write_text(text)
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
