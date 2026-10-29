"""Semantic checks: AST -> list of errors. A schema that passes is safe to lower."""
import re
from schema import Schema, Struct, Field, Switch, Pad, Type

BYTE_INTS = {"u8": 1, "u16": 2, "u32": 4}   # allowed as array elements

# Identifiers the generated C uses for its own types, as <proto>_<name>.
RESERVED_TYPES = {"status", "cstr", "bytes", "rd", "wr"}
# ... and for its own constants and macros, as <PROTO>_<NAME>.
RESERVED_CONSTS = {"OK", "INVALID", "NOSPACE", "TRY", "CHECK", "CSTR", "GEN_H"}

C_KEYWORDS = set("""
    alignas alignof auto bool break case char const constexpr continue default
    do double else enum extern false float for goto if inline int long nullptr
    register restrict return short signed sizeof static static_assert struct
    switch thread_local true typedef typeof typeof_unqual union unsigned void
    volatile while
""".split())


def int_bits(name: str) -> int | None:
    """Width of a built-in integer type u1..u32, or None."""
    m = re.fullmatch(r"u([1-9][0-9]?)", name)
    return int(m[1]) if m and int(m[1]) <= 32 else None


def snake(name: str) -> str:
    """CamelCase -> snake_case: OptionAck -> option_ack, HTTPServer -> http_server."""
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
    return s.lower()


def is_int_field(f, enums) -> bool:
    """A scalar integer or enum field: part of a bit group."""
    return isinstance(f, Field) and not f.type.is_array and \
        (int_bits(f.type.name) is not None or f.type.name in enums)


def field_bits(f: Field, enums) -> int:
    t = f.type.name
    return int_bits(t) if t not in enums else int_bits(enums[t].base)


class SchemaError(Exception):
    def __init__(self, errors: list[tuple[int, str]]):
        super().__init__("\n".join(f"line {l}: {m}" for l, m in errors))
        self.errors = errors


def check(s: Schema) -> None:
    """Raise SchemaError listing every problem found, or return None."""
    errors = _Checker(s).run()
    if errors:
        raise SchemaError(errors)


def size_bounds(s: Schema) -> dict[str, tuple[int, int | None]]:
    """(min, max) encoded size in bytes of every struct; max None = unbounded.
    Only valid for a schema that check() accepted."""
    enums = {e.name: e for e in s.enums}
    structs = {st.name: st for st in s.structs}
    memo: dict[str, tuple[int, int | None]] = {}

    def add(a, b):
        return None if a is None or b is None else a + b

    def roundup(n, align):
        return None if n is None else -(-n // align) * align

    def type_bits(t: Type) -> tuple[int, int | None]:
        if not t.is_array:
            if t.name == "cstr":
                return 8, None
            if t.name in structs:
                lo, hi = struct(t.name)
                return 8 * lo, None if hi is None else 8 * hi
            return 0, 0  # integers are counted by the caller
        if t.count == "..":
            return 0, None
        elem = 8 * BYTE_INTS[t.name]
        if isinstance(t.count, int):
            return elem * t.count, elem * t.count
        return 0, None  # sized: refined by the caller, which knows the length field

    def struct(name: str) -> tuple[int, int | None]:
        if name in memo:
            return memo[name]
        st = structs[name]
        lo, hi = 0, 0
        for f in st.fields:
            if isinstance(f, Pad):
                lo, hi = roundup(lo, 8 * f.align), roundup(hi, 8 * f.align)
            elif isinstance(f, Switch):
                arms = [(0, None) if "_" in a.tags else type_bits(a.type) for a in f.arms]
                lo += min(a[0] for a in arms)
                hi = add(hi, None if any(a[1] is None for a in arms) else max(a[1] for a in arms))
            elif is_int_field(f, enums):
                w = field_bits(f, enums)
                lo, hi = lo + w, add(hi, w)
            elif isinstance(f.type.count, str) and f.type.count != "..":
                n = next(g for g in st.fields if isinstance(g, Field) and g.name == f.type.count)
                hi = add(hi, 8 * ((1 << field_bits(n, enums)) - 1))
            else:
                flo, fhi = type_bits(f.type)
                lo, hi = lo + flo, add(hi, fhi)
        memo[name] = (lo // 8, None if hi is None else hi // 8)
        return memo[name]

    return {name: struct(name) for name in structs}


class _Checker:
    def __init__(self, s: Schema):
        self.s = s
        self.errors: list[tuple[int, str]] = []
        self.enums = {}
        self.structs = {}
        self._open: dict[str, bool] = {}

    def err(self, line: int, msg: str) -> None:
        self.errors.append((line, msg))

    def run(self) -> list[tuple[int, str]]:
        self.top_level()
        for e in self.s.enums:
            self.enum(e)
        for st in self.s.structs:
            self.struct(st)
        self.cycles()
        self.c_names()
        if not self.errors:  # needs a well-formed schema
            self.max_sizes()
        return sorted(self.errors)

    # ---- declarations ----------------------------------------------------

    def top_level(self) -> None:
        s = self.s
        if not s.protocol:
            self.err(1, "missing 'protocol NAME;'")
        if s.endian != "big":
            self.err(1, f"endian {s.endian} is not supported")
        for d in [*s.enums, *s.structs]:
            if d.name == "cstr" or int_bits(d.name) is not None:
                self.err(d.line, f"'{d.name}' is a built-in type")
            elif d.name in self.enums or d.name in self.structs:
                self.err(d.line, f"'{d.name}' is declared twice")
            else:
                (self.enums if d in s.enums else self.structs)[d.name] = d
        if not any(st.is_message for st in s.structs):
            self.err(1, "no message declared")

    def enum(self, e) -> None:
        bits = int_bits(e.base)
        if bits is None:
            self.err(e.line, f"enum {e.name}: base type must be u1..u32, not '{e.base}'")
            return
        names, values = set(), {}
        for v in e.variants:
            if v.name in names:
                self.err(v.line, f"enum {e.name}: variant {v.name} declared twice")
            names.add(v.name)
            if v.value >= 1 << bits:
                self.err(v.line, f"enum {e.name}: {v.name} = {v.value} does not fit in {e.base}")
            if v.value in values:
                self.err(v.line, f"enum {e.name}: {v.name} has the same value as {values[v.value]}")
            values.setdefault(v.value, v.name)

    # ---- structs ---------------------------------------------------------

    def struct(self, st: Struct) -> None:
        if not st.fields:
            self.err(st.line, f"{st.name}: empty structs are not supported")
            return
        if st.max is not None and not st.is_message:
            self.err(st.line, f"{st.name}: only a message can declare 'max'")
        seen: dict[str, Field | Switch] = {}
        lengths: dict[str, str] = {}  # length field -> the array it sizes
        last = len(st.fields) - 1
        for i, f in enumerate(st.fields):
            named = not isinstance(f, Pad) and f.name != "_"
            if named:
                if f.name in seen:
                    self.err(f.line, f"{st.name}: field '{f.name}' declared twice")
                if f.name in C_KEYWORDS:
                    self.err(f.line, f"{st.name}: field name '{f.name}' is a C keyword")
            if isinstance(f, Field):
                self.field(st, f, seen, lengths)
            elif isinstance(f, Switch):
                self.switch(st, f, seen)
            elif f.align < 1:
                self.err(f.line, f"{st.name}: pad({f.align}) must be at least 1")
            if i != last and self.field_is_open(f):
                what = "'[..]' field" if isinstance(f, Field) and f.type.count == ".." \
                    else "field that runs to the end of the message"
                self.err(f.line, f"{st.name}.{f.name}: a {what} must be the last field")
            if named:
                seen[f.name] = f
        self.bit_groups(st)
        stored = [f for f in st.fields if isinstance(f, Switch) or
                  isinstance(f, Field) and f.name != "_" and f.const is None
                  and f.name not in lengths]
        if not stored:
            self.err(st.line, f"{st.name}: has no fields to store (only reserved, constant or "
                              "length fields); not supported yet")

    def field(self, st: Struct, f: Field, earlier: dict, lengths: dict) -> None:
        where = f"{st.name}.{f.name}"
        t = f.type
        if not self.resolve(where, t, f.line):
            return
        if f.const is not None:
            bits = int_bits(t.name)
            if t.is_array or bits is None:
                self.err(f.line, f"{where}: only an integer field can be constant")
            elif f.const >= 1 << bits:
                self.err(f.line, f"{where}: constant {f.const} does not fit in {t.name}")
        elif f.name == "_" and not (int_bits(t.name) is not None and
                                    (not t.is_array or isinstance(t.count, int))):
            self.err(f.line, f"{where}: a reserved field must be an integer or a fixed array")
        if not t.is_array:
            return
        elem_is_struct = t.name in self.structs
        if elem_is_struct:
            if t.count != "..":
                self.err(f.line, f"{where}: an array of structs must be '{t.name}[..]'")
            elif self.struct_is_open(t.name):
                self.err(f.line, f"{where}: {t.name} runs to the end of the message, "
                                 "so it cannot be a list element")
        elif t.name not in BYTE_INTS:
            self.err(f.line, f"{where}: array elements must be u8, u16, u32 or a struct")
        if isinstance(t.count, int) and t.count < 1:
            self.err(f.line, f"{where}: array length must be at least 1")
        if isinstance(t.count, str) and t.count != "..":
            # The length field holds the array's size in bytes; encode
            # computes it, so the caller never sets it.
            if t.name in BYTE_INTS and t.name != "u8":
                self.err(f.line, f"{where}: an array sized by a field must be u8[{t.count}]")
            n = earlier.get(t.count)
            if n is None:
                self.err(f.line, f"{where}: length field '{t.count}' must be declared "
                                 "earlier in the same struct")
            elif not isinstance(n, Field) or n.type.is_array or int_bits(n.type.name) is None:
                self.err(f.line, f"{where}: length field '{t.count}' must be an integer")
            elif n.const is not None:
                self.err(f.line, f"{where}: length field '{t.count}' cannot be constant")
            elif t.count in lengths:
                self.err(f.line, f"{where}: length field '{t.count}' already sizes "
                                 f"'{lengths[t.count]}'")
            else:
                lengths[t.count] = f.name

    def bit_groups(self, st: Struct) -> None:
        """Consecutive integer fields are packed MSB-first into groups that
        end on a byte boundary and span at most 32 bits."""
        acc, start = 0, None
        for f in st.fields:
            if is_int_field(f, self.enums):
                if acc == 0:
                    start = f
                acc += field_bits(f, self.enums)
                if acc > 32:
                    self.err(start.line, f"{st.name}.{start.name}: bitfields from here span more "
                                         "than 32 bits before reaching a byte boundary")
                    acc = 0
                elif acc % 8 == 0:  # a byte boundary closes the group
                    acc = 0
            elif acc:
                self.err(start.line, f"{st.name}.{start.name}: bitfields from here must fill "
                                     "whole bytes")
                acc = 0
        if acc:
            self.err(start.line, f"{st.name}.{start.name}: bitfields from here must fill "
                                 "whole bytes")

    def switch(self, st: Struct, sw: Switch, earlier: dict) -> None:
        where = f"{st.name}.{sw.name}"
        tag = earlier.get(sw.tag)
        enum = None
        if tag is None:
            self.err(sw.line, f"{where}: switch tag '{sw.tag}' must be an earlier "
                              f"field of {st.name}")
        elif not isinstance(tag, Field) or tag.type.is_array or tag.type.name not in self.enums:
            self.err(sw.line, f"{where}: switch tag '{sw.tag}' must have an enum type")
        else:
            enum = self.enums[tag.type.name]
        variants = {v.name for v in enum.variants} if enum else set()
        used: set[str] = set()
        for i, arm in enumerate(sw.arms):
            if "_" in arm.tags:
                self.wild_arm(where, sw, i, arm)
                continue
            for name in arm.tags:
                if enum and name not in variants:
                    self.err(arm.line, f"{where}: {name} is not a variant of {enum.name}")
                elif name in used:
                    self.err(arm.line, f"{where}: {name} appears in more than one arm")
                used.add(name)
            t = arm.type
            if t.is_array or t.name not in self.structs:
                self.err(arm.line, f"{where}: an arm's type must be a struct")
            elif self.structs[t.name].is_message:
                self.err(arm.line, f"{where}: {t.name} is a message and cannot be used as a type")

    def wild_arm(self, where: str, sw: Switch, i: int, arm) -> None:
        if arm.tags != ["_"]:
            self.err(arm.line, f"{where}: '_' must be the only tag in its arm")
        if i != len(sw.arms) - 1:
            self.err(arm.line, f"{where}: the '_' arm must be last")
        if arm.type != Type("u8", "..", True):
            self.err(arm.line, f"{where}: the '_' arm's type must be u8[..]")
        if any(snake(a.type.name) == "unknown" for a in sw.arms if "_" not in a.tags):
            self.err(arm.line, f"{where}: the '_' arm generates 'body.unknown', "
                               "which an arm type named Unknown already uses")

    def resolve(self, where: str, t: Type, line: int) -> bool:
        if t.name == "cstr" or int_bits(t.name) is not None or t.name in self.enums:
            return True
        st = self.structs.get(t.name)
        if st is None:
            self.err(line, f"{where}: unknown type '{t.name}'")
            return False
        if st.is_message:
            self.err(line, f"{where}: {t.name} is a message and cannot be used as a type")
            return False
        return True

    # ---- open-endedness: does a field consume the rest of the message? -----

    def field_is_open(self, f) -> bool:
        if isinstance(f, Pad):
            return False
        if isinstance(f, Switch):
            return any("_" in a.tags or self.type_is_open(a.type) for a in f.arms)
        return self.type_is_open(f.type)

    def type_is_open(self, t: Type) -> bool:
        if t.count == "..":
            return True
        return not t.is_array and t.name in self.structs and self.struct_is_open(t.name)

    def struct_is_open(self, name: str) -> bool:
        if name not in self._open:
            self._open[name] = False  # guards against cycles, which cycles() reports
            fields = self.structs[name].fields
            self._open[name] = any(self.field_is_open(f) for f in fields)
        return self._open[name]

    # ---- whole-schema checks -------------------------------------------------

    def cycles(self) -> None:
        """A struct may not contain itself, directly or through other structs."""
        def children(st: Struct):
            for f in st.fields:
                types = [f.type] if isinstance(f, Field) else \
                        [a.type for a in f.arms] if isinstance(f, Switch) else []
                for t in types:
                    if t.name in self.structs:
                        yield t.name

        state: dict[str, int] = {}  # 1 = on stack, 2 = done
        def visit(name: str, path: list[str]) -> None:
            state[name] = 1
            for child in children(self.structs[name]):
                if state.get(child) == 1:
                    cycle = path[path.index(child):] + [child]
                    self.err(self.structs[child].line,
                             f"{child} contains itself: {' -> '.join(cycle)}")
                elif child not in state:
                    visit(child, path + [child])
            state[name] = 2

        for name in self.structs:
            if name not in state:
                visit(name, [name])

    def max_sizes(self) -> None:
        bounds = size_bounds(self.s)
        for st in self.s.structs:
            if st.max is None:
                continue
            lo, hi = bounds[st.name]
            if st.max < lo:
                self.err(st.line, f"{st.name}: max {st.max} is less than its minimum size {lo}")
            elif hi is not None and st.max >= hi:
                self.err(st.line, f"{st.name}: max {st.max} has no effect: "
                                  f"it can never exceed {hi} bytes")

    def c_names(self) -> None:
        """Distinct schema names must stay distinct once turned into C names."""
        types: dict[str, str] = {r: "the generated code" for r in RESERVED_TYPES}
        def claim(c: str, owner: str, line: int) -> None:
            if c in types and types[c] != owner:
                self.err(line, f"{owner}: C name <proto>_{c} is also used by {types[c]}")
            types.setdefault(c, owner)

        for d in [*self.enums.values(), *self.structs.values()]:
            claim(snake(d.name), d.name, d.line)
        for st in self.structs.values():
            for f in st.fields:
                if isinstance(f, Field) and f.type.count == ".." and f.type.name in self.structs:
                    for suffix in ("_list", "_iter"):
                        claim(snake(f.type.name) + suffix, f"{f.type.name}[..]", f.line)
                elif isinstance(f, Switch):
                    claim(f"{snake(st.name)}_{f.name}", f"switch {st.name}.{f.name}", f.line)

        consts: dict[str, str] = {c: "the generated code" for c in RESERVED_CONSTS}
        def claim_const(c: str, owner: str, line: int) -> None:
            if c in consts and consts[c] != owner:
                self.err(line, f"{owner}: C constant <PROTO>_{c} is also used by {consts[c]}")
            consts.setdefault(c, owner)

        for c in self.s.consts:
            claim_const(snake(c.name).upper(), f"const {c.name}", c.line)
        for e in self.enums.values():
            for v in e.variants:
                claim_const(f"{snake(e.name)}_{snake(v.name)}".upper(), f"{e.name}.{v.name}", v.line)
        for st in self.structs.values():
            if st.is_message:
                for suffix in ("_MIN_SIZE", "_MAX_SIZE"):
                    claim_const(snake(st.name).upper() + suffix, f"message {st.name}", st.line)


if __name__ == "__main__":
    import sys
    from schema import parse
    path = sys.argv[1]
    try:
        check(parse(open(path).read()))
    except SchemaError as e:
        for line, msg in e.errors:
            print(f"{path}:{line}: {msg}", file=sys.stderr)
        sys.exit(1)
    print(f"{path}: ok")
