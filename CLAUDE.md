# pktc

A packet compiler: schema → dependency-free C23 encode/decode code for simple
RFC protocols (TFTP, PCP, ...). Read `DESIGN.md` before making changes. It
records the scope, the schema language, the shape of the generated C, the
validation rules and the next steps.

- The compiler is Python, using Lark: `schema.py` (parse), `check.py`,
  `ir.py`, `emit.py`, run as `python3 pktc.py SCHEMA`. The generated C must
  have no dependencies.
- Run the tests with `python3 -m unittest discover tests` (about 20 s:
  it fuzzes each protocol briefly if clang is installed). After changing
  the emitter, regenerate with `python3 pktc.py
  examples/<proto>/<proto>.schema --fuzz` and review the diff.
- Keep the language small: add a construct only when a target protocol needs
  it, and push one-off oddities to the escape hatch.
- Check generated or example C with
  `gcc -std=c23 -Wall -Wextra -pedantic`.
- Update `DESIGN.md` when a design decision changes.
