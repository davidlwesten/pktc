"""Semantic checks: AST -> list of errors. A schema that passes is safe to lower."""
import re
from schema import Schema, Struct, Field, Switch, Pad, Type

INTS = {"u8": 1, "u16": 2, "u32": 4}
BUILTINS = set(INTS) | {"cstr"}

# Identifiers the generated C uses for its own types, as <proto>_<name>.
RESERVED_TYPES = {"status", "cstr", "bytes", "rd", "wr"}

C_KEYWORDS = set("""
    alignas alignof auto bool break case char const constexpr continue default
    do double else enum extern false float for goto if inline int long nullptr
    register restrict return short signed sizeof static static_assert struct
    switch thread_local true typedef typeof typeof_unqual union unsigned void
    volatile while
""".split())


def snake(name: str) -> str:
    """CamelCase -> snake_case: OptionAck -> option_ack, HTTPServer -> http_server."""
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
    return s.lower()


class SchemaError(Exception):
    def __init__(self, errors: list[tuple[int, str]]):
        super().__init__("\n".join(f"line {l}: {m}" for l, m in errors))
        self.errors = errors


def check(s: Schema) -> None:
    """Raise SchemaError listing every problem found, or return None."""
    errors = _Checker(s).run()
    if errors:
        raise SchemaError(errors)


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
        return sorted(self.errors)

    # ---- declarations ----------------------------------------------------

    def top_level(self) -> None:
        s = self.s
        if not s.protocol:
            self.err(1, "missing 'protocol NAME;'")
        if s.endian != "big":
            self.err(1, f"endian {s.endian} is not supported")
        for d in [*s.enums, *s.structs]:
            if d.name in BUILTINS:
                self.err(d.line, f"'{d.name}' is a built-in type")
            elif d.name in self.enums or d.name in self.structs:
                self.err(d.line, f"'{d.name}' is declared twice")
            else:
                (self.enums if d in s.enums else self.structs)[d.name] = d
        if not any(st.is_message for st in s.structs):
            self.err(1, "no message declared")

    def enum(self, e) -> None:
        if e.base not in INTS:
            self.err(e.line, f"enum {e.name}: base type must be u8, u16 or u32, not '{e.base}'")
            return
        limit = 1 << (8 * INTS[e.base])
        names, values = set(), {}
        for v in e.variants:
            if v.name in names:
                self.err(v.line, f"enum {e.name}: variant {v.name} declared twice")
            names.add(v.name)
            if v.value >= limit:
                self.err(v.line, f"enum {e.name}: {v.name} = {v.value} does not fit in {e.base}")
            if v.value in values:
                self.err(v.line, f"enum {e.name}: {v.name} has the same value as {values[v.value]}")
            values.setdefault(v.value, v.name)

    # ---- structs ---------------------------------------------------------

    def struct(self, st: Struct) -> None:
        if not st.fields:
            self.err(st.line, f"{st.name}: empty structs are not supported")
            return
        seen: dict[str, Field | Switch] = {}
        lengths: dict[str, str] = {}  # length field -> the array it sizes
        last = len(st.fields) - 1
        for i, f in enumerate(st.fields):
            if not isinstance(f, Pad):
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
            if not isinstance(f, Pad):
                seen[f.name] = f

    def field(self, st: Struct, f: Field, earlier: dict, lengths: dict) -> None:
        where = f"{st.name}.{f.name}"
        t = f.type
        if not self.resolve(where, t, f.line):
            return
        if not t.is_array:
            return
        elem_is_struct = t.name in self.structs
        if t.name in self.enums or t.name == "cstr":
            self.err(f.line, f"{where}: arrays of {t.name} are not supported")
        elif elem_is_struct and t.count != "..":
            self.err(f.line, f"{where}: an array of structs must be '{t.name}[..]'")
        elif elem_is_struct and self.struct_is_open(t.name):
            self.err(f.line, f"{where}: {t.name} runs to the end of the message, "
                             "so it cannot be a list element")
        if isinstance(t.count, int) and t.count < 1:
            self.err(f.line, f"{where}: array length must be at least 1")
        if isinstance(t.count, str) and t.count != "..":
            # The length field holds the array's size in bytes; encode
            # computes it, so the caller never sets it.
            if t.name in INTS and t.name != "u8":
                self.err(f.line, f"{where}: an array sized by a field must be u8[{t.count}]")
            n = earlier.get(t.count)
            if n is None:
                self.err(f.line, f"{where}: length field '{t.count}' must be declared "
                                 "earlier in the same struct")
            elif not isinstance(n, Field) or n.type.is_array or n.type.name not in INTS:
                self.err(f.line, f"{where}: length field '{t.count}' must be u8, u16 or u32")
            elif t.count in lengths:
                self.err(f.line, f"{where}: length field '{t.count}' already sizes "
                                 f"'{lengths[t.count]}'")
            else:
                lengths[t.count] = f.name

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
        if t.name in BUILTINS or t.name in self.enums:
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

        consts: dict[str, str] = {c: "the generated code" for c in ("OK", "INVALID", "NOSPACE")}
        for e in self.enums.values():
            for v in e.variants:
                c = f"{snake(e.name)}_{snake(v.name)}".upper()
                owner = f"{e.name}.{v.name}"
                if c in consts and consts[c] != owner:
                    self.err(v.line, f"{owner}: C constant <PROTO>_{c} is also used by {consts[c]}")
                consts.setdefault(c, owner)


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
