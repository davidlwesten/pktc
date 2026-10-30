"""pktc: schema -> <proto>_gen.h + <proto>_gen.c (+ <proto>_fuzz.c).

    python3 pktc.py SCHEMA [-o OUTDIR] [--name NAME] [--fuzz]
    python3 pktc.py --version

OUTDIR defaults to the schema's directory. --name writes NAME.h and NAME.c
(and NAME_fuzz.c) instead; C names still come from the protocol. --fuzz also
writes a libFuzzer harness for the generated code.
"""
import argparse
import pathlib
import sys

from check import SchemaError, check
from emit import emit, emit_fuzz, file_names
from ir import lower
from schema import ParseError, parse

__version__ = "0.1.0"


def compile_schema(path: pathlib.Path, fuzz: bool = False,
                   name: str | None = None) -> tuple[str, dict[str, str]]:
    """Return (proto name, {output file name: text})."""
    s = parse(path.read_text())
    check(s)
    proto = lower(s, path.name)
    h_file, c_file, fuzz_file = file_names(proto.name, name)
    h, c = emit(proto, name)
    files = {h_file: h, c_file: c}
    if fuzz:
        files[fuzz_file] = emit_fuzz(proto, name)
    return proto.name, files


def output_name(text: str) -> str:
    """--name: a bare file name without extension; -o picks the directory."""
    if not text or text in (".", "..") or "/" in text or "\\" in text:
        raise argparse.ArgumentTypeError(
            f"'{text}' is not a file name (use -o for the directory)")
    if text.endswith((".h", ".c")):
        raise argparse.ArgumentTypeError(
            f"'{text}' has an extension: pktc adds .h and .c")
    return text


def main() -> int:
    ap = argparse.ArgumentParser(prog="pktc", description="Generate C23 encode/decode code from a schema.")
    ap.add_argument("schema", type=pathlib.Path)
    ap.add_argument("-o", "--outdir", type=pathlib.Path)
    ap.add_argument("--name", type=output_name,
                    help="write NAME.h and NAME.c instead of <protocol>_gen.h "
                         "and <protocol>_gen.c")
    ap.add_argument("--fuzz", action="store_true", help="also write <proto>_fuzz.c")
    ap.add_argument("--version", action="version", version=f"pktc {__version__}")
    args = ap.parse_args()
    try:
        _, files = compile_schema(args.schema, args.fuzz, args.name)
    except ParseError as e:
        print(f"{args.schema}:{e.line}:{e.column}: {e.msg}", file=sys.stderr)
        return 1
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
