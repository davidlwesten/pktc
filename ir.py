"""Lowering: checked AST -> IR, a flat list of wire operations per struct.

Decode, encode and size are all generated from the same op list, so they
cannot disagree about the wire format. Names in the IR are already C-ready
(snake_case), so the emitter never derives names itself.
"""
from dataclasses import dataclass
from check import INTS, snake
from schema import Schema, Struct, Field, Switch, Pad, Type


# ---- ops ------------------------------------------------------------------

@dataclass
class Int:
    """Big-endian u8/u16/u32. If enum is set, the C field has that enum type."""
    field: str
    size: int
    enum: str | None = None

@dataclass
class Cstr:
    field: str

@dataclass
class Rest:
    """u8[..]: the rest of the message, as a byte view."""
    field: str

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
    """A switch: decodes one union member, chosen by an earlier enum field."""
    field: str
    tag: str
    enum: str
    arms: list[Arm]


# ---- declarations -----------------------------------------------------------

@dataclass
class EnumDecl:
    name: str                         # snake_case
    decl: str                         # schema text for comments
    size: int
    variants: list[tuple[str, int]]   # (C-ready suffix, value)

@dataclass
class StructDecl:
    name: str
    schema_name: str
    decl: tuple[str, list[str]]   # schema text for comments: ("struct S", ["a: u8;", ...])
    is_message: bool
    ops: list
    is_list_elem: bool = False

@dataclass
class Proto:
    name: str
    source: str
    enums: list[EnumDecl]
    structs: list[StructDecl]   # dependencies first; only those reachable from a message


def lower(s: Schema, source: str) -> Proto:
    """Lower a schema that check() accepted."""
    enums = {e.name: e for e in s.enums}
    structs = {st.name: st for st in s.structs}
    out: dict[str, StructDecl] = {}

    def visit(st: Struct) -> None:
        if st.name in out:
            return
        out[st.name] = None  # reserve; check() rules out cycles
        ops = []
        for f in st.fields:
            ops.append(lower_field(st, f))
            for dep in deps(f):
                visit(structs[dep])
        del out[st.name]
        kind = "message" if st.is_message else "struct"
        decl = (f"{kind} {st.name}", [render_field(f) for f in st.fields])
        out[st.name] = StructDecl(snake(st.name), st.name, decl, st.is_message, ops)

    def lower_field(st: Struct, f):
        where = f"{st.name}.{getattr(f, 'name', 'pad')}"
        if isinstance(f, Pad):
            raise NotImplementedError(f"{where}: pad() is not implemented yet")
        if isinstance(f, Switch):
            return lower_switch(st, f, where)
        t = f.type
        if not t.is_array:
            if t.name in INTS:
                return Int(f.name, INTS[t.name])
            if t.name in enums:
                return Int(f.name, INTS[enums[t.name].base], snake(t.name))
            if t.name == "cstr":
                return Cstr(f.name)
            return Sub(f.name, snake(t.name))
        if t.count != "..":
            raise NotImplementedError(f"{where}: {render_type(t)} is not implemented yet")
        if t.name == "u8":
            return Rest(f.name)
        if t.name in structs:
            return List(f.name, snake(t.name))
        raise NotImplementedError(f"{where}: {render_type(t)} is not implemented yet")

    def lower_switch(st: Struct, sw: Switch, where: str) -> Dispatch:
        tag_field = next(f for f in st.fields if isinstance(f, Field) and f.name == sw.tag)
        arms: dict[str, Arm] = {}  # arms with the same type share a union member
        for a in sw.arms:
            if "_" in a.tags:
                raise NotImplementedError(f"{where}: the '_' arm is not implemented yet")
            arm = arms.setdefault(a.type.name, Arm([], snake(a.type.name), snake(a.type.name)))
            arm.tags += [snake(t).upper() for t in a.tags]
        return Dispatch(sw.name, sw.tag, snake(tag_field.type.name), list(arms.values()))

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
        enums=[EnumDecl(snake(e.name), f"enum {e.name} : {e.base}", INTS[e.base],
                        [(snake(v.name).upper(), v.value) for v in e.variants])
               for e in s.enums],
        structs=decls,
    )


# ---- schema text, for comments in the generated code -----------------------------

def render_type(t: Type) -> str:
    return f"{t.name}[{t.count}]" if t.is_array else t.name

def render_field(f) -> str:
    if isinstance(f, Pad):
        return f"pad({f.align});"
    if isinstance(f, Switch):
        return f"{f.name}: switch ({f.tag}) {{ ... }};"
    return f"{f.name}: {render_type(f.type)};"
