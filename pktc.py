"""pktc: schema -> <proto>_gen.h + <proto>_gen.c.

    python3 pktc.py SCHEMA [-o OUTDIR]

OUTDIR defaults to the schema's directory.
"""
import argparse
import pathlib
import sys

from check import SchemaError, check
from emit import emit
from ir import lower
from schema import parse


def compile_schema(path: pathlib.Path) -> tuple[str, str, str]:
    """Return (proto name, header text, source text)."""
    s = parse(path.read_text())
    check(s)
    proto = lower(s, path.name)
    return (proto.name, *emit(proto))


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate C23 encode/decode code from a schema.")
    ap.add_argument("schema", type=pathlib.Path)
    ap.add_argument("-o", "--outdir", type=pathlib.Path)
    args = ap.parse_args()
    try:
        name, h, c = compile_schema(args.schema)
    except SchemaError as e:
        for line, msg in e.errors:
            print(f"{args.schema}:{line}: {msg}", file=sys.stderr)
        return 1
    except NotImplementedError as e:
        print(f"{args.schema}: {e}", file=sys.stderr)
        return 1
    outdir = args.outdir or args.schema.parent
    for suffix, text in ((".h", h), (".c", c)):
        path = outdir / f"{name}_gen{suffix}"
        path.write_text(text)
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
