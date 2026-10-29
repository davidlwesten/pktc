"""Schema front end: text -> typed AST."""
from dataclasses import dataclass, field as dc_field
from lark import Lark, Transformer, v_args

GRAMMAR = r"""
start: decl*

?decl: "protocol" NAME ";"                      -> protocol
     | "endian" ENDIAN ";"                      -> endian
     | "enum" NAME ":" NAME "{" variants "}"    -> enum
     | KIND NAME "{" field* "}"                 -> struct

KIND: "message" | "struct"
ENDIAN: "big" | "little"

variants: variant ("," variant)* ","?
variant: NAME "=" INT

?field: NAME ":" type ("=" "sizeof" "(" NAME ")")? ";"   -> field
      | NAME ":" "switch" "(" NAME ")" "{" arm+ "}" ";"   -> switch
      | "pad" "(" INT ")" ";"                            -> pad

arm: (NAME | WILD) ("," NAME)* "=>" type ","
WILD: "_"

type: NAME                     -> scalar
    | NAME "[" INT "]"         -> fixed_array
    | NAME "[" NAME "]"        -> sized_array
    | NAME "[" ".." "]"        -> rest_array

NAME: /[A-Za-z][A-Za-z0-9_]*/
INT: /0x[0-9a-fA-F]+|[0-9]+/

%ignore /\s+/
%ignore /\/\/[^\n]*/
"""

@dataclass
class Type:
    name: str
    count: int | str | None = None   # int = fixed, str = length field, ".." = to end
    is_array: bool = False

@dataclass
class Field:
    name: str
    type: Type
    size_of: str | None = None
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Arm:
    tags: list[str]
    type: Type
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Switch:
    name: str
    tag: str
    arms: list[Arm]
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Pad:
    align: int
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Variant:
    name: str
    value: int
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Enum:
    name: str
    base: str
    variants: list[Variant]
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Struct:
    name: str
    is_message: bool
    fields: list
    line: int = dc_field(default=0, repr=False, compare=False)

@dataclass
class Schema:
    protocol: str
    endian: str
    enums: list[Enum]
    structs: list[Struct]

class ToAst(Transformer):
    def NAME(self, t): return str(t)
    def INT(self, t): return int(t, 0)
    def WILD(self, t): return "_"
    def protocol(self, c): return ("protocol", c[0])
    def endian(self, c): return ("endian", str(c[0]))
    @v_args(meta=True)
    def variant(self, m, c): return Variant(c[0], c[1], m.line)
    def variants(self, c): return c
    @v_args(meta=True)
    def enum(self, m, c): return Enum(c[0], c[1], c[2], m.line)
    @v_args(meta=True)
    def struct(self, m, c): return Struct(c[1], str(c[0]) == "message", c[2:], m.line)
    def scalar(self, c): return Type(c[0])
    def fixed_array(self, c): return Type(c[0], c[1], True)
    def sized_array(self, c): return Type(c[0], c[1], True)
    def rest_array(self, c): return Type(c[0], "..", True)
    @v_args(meta=True)
    def field(self, m, c): return Field(c[0], c[1], c[2] if len(c) > 2 else None, m.line)
    @v_args(meta=True)
    def arm(self, m, c): return Arm(c[:-1], c[-1], m.line)
    @v_args(meta=True)
    def switch(self, m, c): return Switch(c[0], c[1], c[2:], m.line)
    @v_args(meta=True)
    def pad(self, m, c): return Pad(c[0], m.line)
    def start(self, decls):
        s = Schema("", "big", [], [])
        for d in decls:
            if isinstance(d, Enum): s.enums.append(d)
            elif isinstance(d, Struct): s.structs.append(d)
            else: setattr(s, d[0], d[1])
        return s

_parser = Lark(GRAMMAR, parser="lalr", propagate_positions=True)

def parse(text: str) -> Schema:
    return ToAst().transform(_parser.parse(text))

if __name__ == "__main__":
    import sys, pprint
    pprint.pprint(parse(open(sys.argv[1]).read()), width=100)
