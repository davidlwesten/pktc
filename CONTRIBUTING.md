# Contributing to pktc

Read [`DESIGN.md`](DESIGN.md) before making changes. It records the scope,
the schema language and the reasons behind it, the shape of the generated
C, the validation rules, and the roadmap.

## Setup

You need Python 3.10 or later and a C23 compiler (GCC 14 or later, or a
recent Clang). Clang also lets the tests run the fuzzers.

```sh
pip install -r requirements.txt   # Lark, pinned
make test                         # about 30 s
```

## Layout

The compiler is Python, run as `python3 pktc.py SCHEMA`:

| File | Stage |
|---|---|
| `schema.py` | parse: Lark grammar to an AST |
| `check.py` | check: every rule, with line numbers |
| `ir.py` | lower: the AST to a list of wire operations per struct |
| `emit.py` | emit: the IR to C, plus the fuzz harness |
| `pktc.py` | command line |

Each example in `examples/<proto>/` has a schema, its generated code
(committed), and hand-written tests.

## Making changes

- **Tests:** `make test` runs the compiler tests. It also fuzzes each
  protocol for 3 s if clang is installed; `PKTC_FUZZ_SECONDS=0` only
  builds the fuzz harnesses. `make check` builds and runs each example's C
  tests with sanitizers.
- **Changing the emitter:** run `make examples` and review the diff of
  the generated code. The tests fail if the committed examples are stale.
  Then fuzz for longer: `make fuzz` (60 s per example), or
  `make fuzz-stun FUZZ_SECONDS=600`. CI never runs the fuzzers.
- **Checking C by hand:** compile generated or example C with
  `gcc -std=c23 -Wall -Wextra -pedantic`. Generated code must compile
  without warnings and fit in 79 columns.
- **The generated C must have no dependencies** beyond `<string.h>`,
  `<stdint.h>`, `<stddef.h>` and `<stdbool.h>`.
- **Keep the language small.** Add a construct only when a real protocol
  needs it, and push one-off oddities to the escape hatch.
- **Update `DESIGN.md`** when a design decision changes, including why.
- **Licensing:** pktc is MIT. Keep dependencies MIT-compatible: Lark is
  bundled into `pktc.pyz`.

## Releasing

1. Bump `__version__` in `pktc.py` and commit.
2. Push a tag `v<version>`, e.g. `v0.2.0`.
3. `.github/workflows/release.yml` checks that the tag matches, runs the
   tests, and publishes `pktc.pyz` to a GitHub release.
