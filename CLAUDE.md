# pktc

A packet compiler: schema → dependency-free C23 encode/decode code for simple
RFC protocols (TFTP, PCP, NTP, STUN, ...). Read `DESIGN.md` before making
changes. It records the scope, the schema language, the shape of the
generated C, the validation rules and the next steps.

- The compiler is Python, using Lark: `schema.py` (parse), `check.py`,
  `ir.py`, `emit.py`, run as `python3 pktc.py SCHEMA`. The generated C must
  have no dependencies. `make` builds `pktc.pyz`, a single file with Lark
  inside. pktc is MIT; keep dependencies MIT-compatible.
- Run the tests with `make test` (about 30 s: it fuzzes each protocol
  briefly if clang is installed). After changing the emitter, run `make
  examples` and review the diff. `make check` runs the C tests, `make
  fuzz-<proto> FUZZ_SECONDS=600` a long fuzz run.
- Keep the language small: add a construct only when a target protocol needs
  it, and push one-off oddities to the escape hatch.
- Check generated or example C with
  `gcc -std=c23 -Wall -Wextra -pedantic`.
- Update `DESIGN.md` when a design decision changes.
