# pktc

A packet compiler: schema → dependency-free C23 encode/decode code for simple
RFC protocols (TFTP, PCP, ...). Read `DESIGN.md` before making changes. It
records the scope, the schema language, the shape of the generated C, the
validation rules and the next steps.

- The compiler is Python (`schema.py`, using Lark). The generated C must have
  no dependencies.
- Keep the language small: add a construct only when a target protocol needs
  it, and push one-off oddities to the escape hatch.
- Check generated or example C with
  `gcc -std=c23 -Wall -Wextra -pedantic`.
- Update `DESIGN.md` when a design decision changes.
