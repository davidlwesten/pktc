"""Lowering: checked AST -> IR, a flat list of wire operations per struct.

Decode, encode and size are all generated from the same op list, so they
cannot disagree about the wire format. Names in the IR are already C-ready
(snake_case), so the emitter never derives names itself.
"""
from dataclasses import dataclass
from check import BYTE_INTS, field_bits, int_bits, is_int_field, size_bounds, snake
from schema import Schema, Struct, Field, Switch, Pad as PadField, Type


# ---- ops ------------------------------------------------------------------

@dataclass
class Bits:
    """One field of a Group. At most one of field/const/length_of is set;
    if none is, the bits are reserved."""
    width: int
    shift: int                     # from the least significant bit of the group
    field: str | None = None       # stored in the C struct
    enum: str | None = None        # ... with this enum type
    const: int | None = None       # must equal this; not stored
    length_of: str | None = None   # holds the length of this u8[] field; not stored

@dataclass
class Group:
    """1-4 bytes read as one big-endian integer and split MSB-first into
    fields. A plain u16 field is a group of one 16-bit field."""
    size: int
    bits: list[Bits]

@dataclass
class Cstr:
    field: str

@dataclass
class Rest:
    """u8[..]: the rest of the message, as a byte view."""
    field: str

@dataclass
class Bytes:
    """u8[len]: a byte view whose length is an earlier Bits field."""
    field: str
    len_width: int

@dataclass
class Fixed:
    """A fixed-size integer array, copied into the C struct.
    field None means reserved: skipped on decode, zeros on encode."""
    field: str | None
    elem: int    # bytes per element
    count: int

@dataclass
class Pad:
    """Skip to the next multiple of align bytes from the start of the struct."""
    align: int

@dataclass
class Sub:
    """A nested struct."""
    field: str
    struct: str

@dataclass
class List:
    """S[..]: structs repeated until the end of the message."""
    field: str
    elem: str

@dataclass
class Arm:
    tags: list[str]       # C-ready enum suffixes, e.g. ["RRQ", "WRQ"]
    member: str           # union member, e.g. "request"
    struct: str

@dataclass
class Dispatch:
    """A switch: decodes one union member, chosen by an earlier enum field.
    With a fallback, unknown tags decode the rest into the member 'unknown'."""
    field: str
    tag: str
    enum: str
    arms: list[Arm]
    fallback: bool = False


# ---- declarations -----------------------------------------------------------

@dataclass
class EnumDecl:
    name: str                         # snake_case
    decl: str                         # schema text for comments
    width: int                        # bits
    variants: list[tuple[str, int]]   # (C-ready suffix, value)

@dataclass
class StructDecl:
    name: str
    schema_name: str
    decl: tuple[str, list[str]]   # schema text for comments: ("struct S", ["a: u8;", ...])
    is_message: bool
    ops: list
    is_list_elem: bool = False
    min_size: int = 0
    max_size: int | None = None   # None: unbounded
    max_declared: bool = False    # max_size comes from `max N` and is enforced

@dataclass
class Proto:
    name: str
    source: str
    consts: list[tuple[str, int]]   # (C-ready name, value)
    enums: list[EnumDecl]
    structs: list[StructDecl]   # dependencies first; only those reachable from a message


def lower(s: Schema, source: str) -> Proto:
    """Lower a schema that check() accepted."""
    enums = {e.name: e for e in s.enums}
    structs = {st.name: st for st in s.structs}
    bounds = size_bounds(s)
    out: dict[str, StructDecl] = {}

    def visit(st: Struct) -> None:
        if st.name in out:
            return
        out[st.name] = None  # reserve; check() rules out cycles
        ops = lower_fields(st)
        for f in st.fields:
            for dep in deps(f):
                visit(structs[dep])
        del out[st.name]
        kind = "message" if st.is_message else "struct"
        decl = (f"{kind} {st.name}" + (f" max {st.max}" if st.max is not None else ""),
                [render_field(f) for f in st.fields])
        lo, hi = bounds[st.name]
        out[st.name] = StructDecl(snake(st.name), st.name, decl, st.is_message, ops,
                                  min_size=lo, max_size=st.max if st.max is not None else hi,
                                  max_declared=st.max is not None)

    def lower_fields(st: Struct) -> list:
        lengths = {f.type.count: f.name for f in st.fields
                   if isinstance(f, Field) and isinstance(f.type.count, str)
                   and f.type.count != ".."}
        widths = {f.name: field_bits(f, enums) for f in st.fields if is_int_field(f, enums)}
        ops, group = [], []
        for f in st.fields:
            if is_int_field(f, enums):
                group.append(f)
                total = sum(field_bits(g, enums) for g in group)
                if total % 8 == 0:
                    ops.append(lower_group(group, total, lengths))
                    group = []
            else:
                ops.append(lower_field(st, f, widths))
        return ops

    def lower_group(fields: list[Field], total: int, lengths: dict) -> Group:
        bits, shift = [], total
        for f in fields:
            w = field_bits(f, enums)
            shift -= w
            b = Bits(w, shift)
            if f.const is not None:
                b.const = f.const
            elif f.name in lengths:
                b.length_of = lengths[f.name]
            elif f.name != "_":
                b.field = f.name
                b.enum = snake(f.type.name) if f.type.name in enums else None
            bits.append(b)
        return Group(total // 8, bits)

    def lower_field(st: Struct, f, widths: dict):
        if isinstance(f, PadField):
            return Pad(f.align)
        if isinstance(f, Switch):
            return lower_switch(st, f)
        t = f.type
        if not t.is_array:
            if t.name == "cstr":
                return Cstr(f.name)
            return Sub(f.name, snake(t.name))
        if isinstance(t.count, int):
            return Fixed(None if f.name == "_" else f.name, BYTE_INTS[t.name], t.count)
        if t.count != "..":
            return Bytes(f.name, widths[t.count])
        if t.name == "u8":
            return Rest(f.name)
        return List(f.name, snake(t.name))

    def lower_switch(st: Struct, sw: Switch) -> Dispatch:
        tag_field = next(f for f in st.fields if isinstance(f, Field) and f.name == sw.tag)
        arms: dict[str, Arm] = {}  # arms with the same type share a union member
        fallback = False
        for a in sw.arms:
            if "_" in a.tags:
                fallback = True
                continue
            arm = arms.setdefault(a.type.name, Arm([], snake(a.type.name), snake(a.type.name)))
            arm.tags += [snake(t).upper() for t in a.tags]
        return Dispatch(sw.name, sw.tag, snake(tag_field.type.name), list(arms.values()),
                        fallback)

    def deps(f) -> list[str]:
        if isinstance(f, Field):
            return [f.type.name] if f.type.name in structs else []
        if isinstance(f, Switch):
            return [a.type.name for a in f.arms if a.type.name in structs]
        return []

    for st in s.structs:
        if st.is_message:
            visit(st)

    decls = list(out.values())
    elems = {op.elem for d in decls for op in d.ops if isinstance(op, List)}
    for d in decls:
        d.is_list_elem = d.name in elems
    return Proto(
        name=s.protocol,
        source=source,
        consts=[(snake(c.name).upper(), c.value) for c in s.consts],
        enums=[EnumDecl(snake(e.name), f"enum {e.name} : {e.base}", int_bits(e.base),
                        [(snake(v.name).upper(), v.value) for v in e.variants])
               for e in s.enums],
        structs=decls,
    )


# ---- schema text, for comments in the generated code -----------------------------

def render_type(t: Type) -> str:
    return f"{t.name}[{t.count}]" if t.is_array else t.name

def render_field(f) -> str:
    if isinstance(f, PadField):
        return f"pad({f.align});"
    if isinstance(f, Switch):
        return f"{f.name}: switch ({f.tag}) {{ ... }};"
    const = f" = {f.const}" if f.const is not None else ""
    return f"{f.name}: {render_type(f.type)}{const};"
