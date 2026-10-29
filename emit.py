"""Emit: IR -> <proto>_gen.h and <proto>_gen.c."""
import re
from string import Template

from ir import (Proto, StructDecl, EnumDecl, Group, Bits, Cstr, Rest, Bytes, Fixed, Pad,
                Sub, List, Dispatch)

WIDTH = 79


def c_uint(bits: int) -> str:
    """Smallest uintN_t holding `bits` bits."""
    return "uint8_t" if bits <= 8 else "uint16_t" if bits <= 16 else "uint32_t"


def c_bits(bits: int) -> int:
    return 8 if bits <= 8 else 16 if bits <= 16 else 32


# ---- formatting helpers -------------------------------------------------------

def sig(head: str, params: list[str], end: str) -> str:
    """`head(a, b, ...)end`, wrapped at WIDTH with parameters aligned after '('."""
    one = f"{head}({', '.join(params)}){end}"
    if len(one) <= WIDTH:
        return one
    indent = " " * (len(head) + 1)
    lines, cur = [], f"{head}({params[0]}"
    for i, p in enumerate(params[1:], 2):
        tail = f"){end}" if i == len(params) else ","
        if len(cur) + 2 + len(p) + len(tail) <= WIDTH:
            cur += ", " + p
        else:
            lines.append(cur + ",")
            cur = indent + p
    lines.append(cur + f"){end}")
    return "\n".join(lines)


def sum_expr(lead: str, terms: list[str], end: str) -> str:
    """`<lead>a + b + c<end>`, one term per line if it does not fit."""
    one = f"{lead}{' + '.join(terms)}{end}"
    if len(one) <= WIDTH:
        return one
    pad = " " * (len(lead) - 2)  # operands line up under the first term
    return "\n".join([f"{lead}{terms[0]}"] + [f"{pad}+ {t}" for t in terms[1:]]) + end


def join_wrapped(lead: str, terms: list[str], sep: str, end: str) -> str:
    """`<lead>a<sep>b<sep>c<end>`, wrapped at WIDTH with terms aligned after lead."""
    lines, cur = [], lead + terms[0]
    for i, t in enumerate(terms[1:], 2):
        tail = end if i == len(terms) else sep.rstrip()
        if len(cur) + len(sep) + len(t) + len(tail) <= WIDTH:
            cur += sep + t
        else:
            lines.append(cur + sep.rstrip())
            cur = " " * len(lead) + t
    return "\n".join(lines + [cur + end])


def fold(terms: list[int | str]) -> list[str]:
    """Add up the constant terms and put them first: [2, 'x', 4] -> ['6', 'x']."""
    fixed = sum(t for t in terms if isinstance(t, int))
    return ([str(fixed)] if fixed else []) + [t for t in terms if isinstance(t, str)]


def rule(title: str) -> str:
    head = f"/* ---- {title} "
    return head + "-" * max(4, WIDTH - len(head) - 3) + " */"


def decl_comment(decl: tuple[str, list[str]]) -> str:
    """The schema declaration as a comment, one field per line if it is long."""
    head, fields = decl
    one = f"/* {head} {{ {' '.join(fields)} }} */"
    if len(one) <= WIDTH:
        return one
    return "\n".join([f"/* {head} {{", *[f" *     {f}" for f in fields], " * } */"])


def table(rows: list[tuple[str, str]], sep: str) -> list[str]:
    """Align the second column of rows: `a<pad>sep b`."""
    w = max(len(a) for a, _ in rows)
    return [f"{a.ljust(w)}{sep}{b}" for a, b in rows]


def finish(lines: list[str], macro: str, last: str) -> list[str]:
    """Turn a trailing `    MACRO(expr);` into `    return expr;`, else append `last`."""
    if lines:
        m = re.fullmatch(rf"    {macro}\((.*)\);", lines[-1])
        if m:
            return lines[:-1] + [f"    return {m[1]};"]
    return lines + [last]


class Emitter:
    def __init__(self, proto: Proto):
        self.proto = proto
        self.p = proto.name.lower()
        self.P = proto.name.upper()
        ops = [op for st in proto.structs for op in st.ops]
        self.uses_cstr = any(isinstance(op, Cstr) for op in ops)
        self.uses_bytes = any(isinstance(op, (Rest, Bytes)) or
                              isinstance(op, Dispatch) and op.fallback for op in ops)

    def t(self, text: str, **kw) -> str:
        return Template(text).substitute(p=self.p, P=self.P, **kw)

    # ---- header ----------------------------------------------------------

    def header(self) -> str:
        guard = f"{self.P}_GEN_H"
        out = [f"/* Generated from {self.proto.source} -- do not edit. */",
               f"#ifndef {guard}", f"#define {guard}", "",
               "#include <stdbool.h>", "#include <stddef.h>", "#include <stdint.h>", "",
               self.t(H_STATUS)]
        if self.proto.consts:
            rows = [(f"#define {self.P}_{n}", str(v)) for n, v in self.proto.consts]
            out += [f"/* Constants from {self.proto.source}. */", *table(rows, " "), ""]
        out += [self.h_enum(e) for e in self.proto.enums]
        if self.uses_cstr:
            out.append(self.t(H_CSTR))
        if self.uses_bytes:
            out.append(self.t(H_BYTES))
        for st in self.proto.structs:
            out.append(self.h_struct(st))
            if st.is_message:
                out.append(self.h_sizes(st))
            if st.is_list_elem:
                out.append(self.t(H_LIST, s=st.name, S=st.schema_name))
        for st in self.proto.structs:
            if st.is_message:
                out.append(self.h_message_api(st))
        for st in self.proto.structs:
            if st.is_list_elem:
                out.append(self.h_iter_api(st))
        if self.uses_cstr:
            out.append(self.t(H_CSTR_MACRO))
        out.append("#endif")
        return "\n".join(out) + "\n"

    def h_enum(self, e: EnumDecl) -> str:
        rows = [(f"    {self.P}_{e.name.upper()}_{v}", f" {n},") for v, n in e.variants]
        return "\n".join([f"/* {e.decl} */",
                          f"enum {self.p}_{e.name} : {c_uint(e.width)} {{",
                          *table(rows, " ="), "};", ""])

    def member_type(self, op) -> str:
        p = self.p
        match op:
            case Bits(enum=None):
                return c_uint(op.width)
            case Bits():
                return f"enum {p}_{op.enum}"
            case Cstr():
                return f"struct {p}_cstr"
            case Rest() | Bytes():
                return f"struct {p}_bytes"
            case Sub():
                return f"struct {p}_{op.struct}"
            case List():
                return f"struct {p}_{op.elem}_list"
        raise AssertionError(op)

    def h_struct(self, st: StructDecl) -> str:
        p = self.p
        tags = {op.tag: op.field for op in st.ops if isinstance(op, Dispatch)}
        out = [decl_comment(st.decl), f"struct {p}_{st.name} {{"]
        for op in st.ops:
            match op:
                case Group():
                    for b in op.bits:
                        if b.field is None:
                            continue
                        line = f"    {self.member_type(b)} {b.field};"
                        if b.field in tags:
                            line += f"  /* selects {tags[b.field]} */"
                        out.append(line)
                case Fixed(field=None) | Pad():
                    pass
                case Fixed():
                    out.append(f"    {c_uint(8 * op.elem)} {op.field}[{op.count}];")
                case Dispatch():
                    arms = [(f"struct {p}_{a.struct}", a.member, ", ".join(a.tags))
                            for a in op.arms]
                    if op.fallback:
                        arms.append((f"struct {p}_bytes", "unknown", "any other value"))
                    rows = table([(f"        {t}", f" {m};") for t, m, _ in arms], "")
                    rows = table([(r, f"  /* {c} */") for r, (_, _, c) in zip(rows, arms)], "")
                    out += ["    union {", *rows, f"    }} {op.field};"]
                case _:
                    out.append(f"    {self.member_type(op)} {op.field};")
        out += ["};", ""]
        return "\n".join(out)

    def h_sizes(self, st: StructDecl) -> str:
        m = f"{self.P}_{st.name.upper()}"
        if st.max_declared:
            comment = [f"/* Size limits of an encoded {st.schema_name}, in bytes. Decode and "
                       "encode", f" * reject a {st.schema_name} larger than {m}_MAX_SIZE. */"]
        else:
            comment = [f"/* Size limits of an encoded {st.schema_name}, in bytes. */"]
        rows = [(f"#define {m}_MIN_SIZE", str(st.min_size))]
        if st.max_size is not None:
            rows.append((f"#define {m}_MAX_SIZE", str(st.max_size)))
        return "\n".join([*comment, *table(rows, " "), ""])

    def h_message_api(self, st: StructDecl) -> str:
        p, m = self.p, f"{self.p}_{st.name}"
        return "\n".join([
            f"/* Decode one {st.schema_name}. Views in *out borrow buf: keep buf alive and",
            " * unchanged while using them. */",
            sig(f"{p}_status {m}_decode",
                [f"struct {m} *out", "const uint8_t *buf", "size_t len"], ";"),
            "",
            f"/* Encode into buf[0..cap). On {self.P}_OK, *written is the encoded length. */",
            sig(f"{p}_status {m}_encode",
                [f"const struct {m} *in", "uint8_t *buf", "size_t cap", "size_t *written"], ";"),
            "",
            f"/* Exact number of bytes {m}_encode will write, if it succeeds. */",
            sig(f"size_t {m}_encoded_size", [f"const struct {m} *in"], ";"),
            ""])

    def h_iter_api(self, st: StructDecl) -> str:
        s = f"{self.p}_{st.name}"
        return "\n".join([
            f"/* Iterate a decoded {st.schema_name}[..]. Decode already validated it,",
            " * so next() only returns false at the end. */",
            sig(f"void {s}_iter_init", [f"struct {s}_iter *it", f"const struct {s}_list *list"], ";"),
            sig(f"bool {s}_iter_next", [f"struct {s}_iter *it", f"struct {s} *out"], ";"),
            ""])

    # ---- source --------------------------------------------------------------

    def source(self) -> str:
        body = []
        for st in self.proto.structs:
            kind = "message" if st.is_message else "struct"
            body += [rule(f"{kind} {st.schema_name}"), "", self.c_struct(st)]
            if st.is_list_elem:
                body += [rule(f"{st.schema_name}[..]"), "", self.c_list(st)]
        body = "\n".join(body)
        out = [f"/* Generated from {self.proto.source} -- do not edit. */",
               f'#include "{self.p}_gen.h"', "", "#include <string.h>", "",
               rule("wire primitives"), "", self.primitives(body), body]
        return "\n".join(out).rstrip("\n") + "\n"

    def primitives(self, body: str) -> str:
        """The cursor types, plus every primitive the generated code calls."""
        texts = {k: self.t(v) for k, v in PRIMITIVES}
        def called(k: str, text: str) -> bool:
            return f"{self.P if k.isupper() else self.p}_{k}(" in text
        used, changed = set(), True
        while changed:  # primitives call each other: repeat until nothing new
            search = body + "".join(texts[k] for k in used)
            new = {k for k in texts if k not in used and called(k, search)}
            used |= new
            changed = bool(new)
        return "\n".join([self.t(C_CURSORS)] + [texts[k] for k, _ in PRIMITIVES if k in used])

    def c_struct(self, st: StructDecl) -> str:
        p, P, s = self.p, self.P, f"{self.p}_{st.name}"
        has_pad = any(isinstance(op, Pad) for op in st.ops)
        out = []
        for op in st.ops:
            if isinstance(op, Dispatch):
                out.append(self.c_dispatch(st, op))

        lines = ["    const uint8_t *start = r->p;"] if has_pad else []
        for i, op in enumerate(st.ops):
            lines += self.read(op, st, f"bits{i}")
        out += [sig(f"static bool {s}_read", [f"struct {p}_rd *r", f"struct {s} *out"], " {"),
                *finish(lines, f"{P}_CHECK", "    return true;"), "}", ""]

        lines = ["    uint8_t *start = w->p;"] if has_pad else []
        for op in st.ops:
            lines += self.write(op, st)
        out += [sig(f"static {p}_status {s}_write",
                    [f"struct {p}_wr *w", f"const struct {s} *in"], " {"),
                *finish(lines, f"{P}_TRY", f"    return {P}_OK;"), "}", ""]

        out += [sig(f"static size_t {s}_size", [f"const struct {s} *in"], " {"),
                *self.size_body(st), "}", ""]

        if st.is_message:
            out.append(self.c_message(st))
        return "\n".join(out)

    # ---- per-op code ---------------------------------------------------------

    def read(self, op, st: StructDecl, local: str) -> list[str]:
        p, C = self.p, f"    {self.P}_CHECK"
        match op:
            case Group():
                return self.read_group(op, local)
            case Cstr():
                return [f"{C}({p}_rd_cstr(r, &out->{op.field}));"]
            case Rest():
                return [f"{C}({p}_rd_rest(r, &out->{op.field}));"]
            case Bytes():
                return [f"{C}({p}_rd_bytes(r, &out->{op.field}, {op.field}_len));"]
            case Fixed(field=None):
                return [f"{C}({p}_rd_skip(r, {op.elem * op.count}));"]
            case Fixed(elem=1):
                return [f"{C}({p}_rd_raw(r, out->{op.field}, {op.count}));"]
            case Fixed():
                return [f"    for (size_t i = 0; i < {op.count}; i++)",
                        f"    {C}({p}_rd_u{8 * op.elem}(r, &out->{op.field}[i]));"]
            case Pad():
                return [f"{C}({p}_rd_skip(r, {p}_pad_len((size_t)(r->p - start), {op.align})));"]
            case Sub():
                return [f"{C}({p}_{op.struct}_read(r, &out->{op.field}));"]
            case List():
                return [f"{C}({p}_{op.elem}_list_read(r, &out->{op.field}));"]
            case Dispatch():
                return [f"{C}({p}_{st.name}_{op.field}_read(r, out));"]
        raise AssertionError(op)

    def read_group(self, g: Group, local: str) -> list[str]:
        p, C = self.p, f"    {self.P}_CHECK"
        total, rd = 8 * g.size, f"{p}_rd_u{8 * g.size}"
        if all(b.field is None and b.const is None and b.length_of is None for b in g.bits):
            return [f"{C}({p}_rd_skip(r, {g.size}));"]
        if len(g.bits) == 1 and g.bits[0].field is not None:
            return [f"{C}({rd}(r, &out->{g.bits[0].field}));"]
        if len(g.bits) == 1 and g.bits[0].length_of is not None:
            n = f"{g.bits[0].length_of}_len"
            return [f"    {c_uint(total)} {n};", f"{C}({rd}(r, &{n}));"]
        lines = [f"    {c_uint(total)} {local};", f"{C}({rd}(r, &{local}));"]
        for b in g.bits:
            shifted = f"{local} >> {b.shift}" if b.shift else local
            if b.shift + b.width == total:  # top bits: nothing above to mask off
                e = shifted
            elif b.shift:
                e = f"({shifted}) & {hex((1 << b.width) - 1)}"
            else:
                e = f"{local} & {hex((1 << b.width) - 1)}"
            if b.field is not None:
                lines.append(f"    out->{b.field} = ({self.member_type(b)})({e});")
            elif b.length_of is not None:
                t = c_uint(b.width)
                lines.append(f"    {t} {b.length_of}_len = ({t})({e});")
            elif b.const is not None:
                lines.append(f"{C}({e if e == local else f'({e})'} == {b.const});")
        return lines

    def write(self, op, st: StructDecl) -> list[str]:
        p, T = self.p, f"    {self.P}_TRY"
        match op:
            case Group():
                return self.write_group(op)
            case Cstr():
                return [f"{T}({p}_wr_cstr(w, &in->{op.field}));"]
            case Rest() | Bytes():
                return [f"{T}({p}_wr_raw(w, in->{op.field}.ptr, in->{op.field}.len));"]
            case Fixed(field=None):
                return [f"{T}({p}_wr_zero(w, {op.elem * op.count}));"]
            case Fixed(elem=1):
                return [f"{T}({p}_wr_raw(w, in->{op.field}, {op.count}));"]
            case Fixed():
                return [f"    for (size_t i = 0; i < {op.count}; i++)",
                        f"    {T}({p}_wr_u{8 * op.elem}(w, in->{op.field}[i]));"]
            case Pad():
                return [f"{T}({p}_wr_zero(w, {p}_pad_len((size_t)(w->p - start), {op.align})));"]
            case Sub():
                return [f"{T}({p}_{op.struct}_write(w, &in->{op.field}));"]
            case List():
                return [f"{T}({p}_{op.elem}_list_write(w, &in->{op.field}));"]
            case Dispatch():
                return [f"{T}({p}_{st.name}_{op.field}_write(w, in));"]
        raise AssertionError(op)

    def write_group(self, g: Group) -> list[str]:
        p, P, T = self.p, self.P, f"    {self.P}_TRY"
        total, wr, ct = 8 * g.size, f"{p}_wr_u{8 * g.size}", c_uint(8 * g.size)
        if all(b.field is None and b.const is None and b.length_of is None for b in g.bits):
            return [f"{T}({p}_wr_zero(w, {g.size}));"]
        if len(g.bits) == 1 and g.bits[0].field is not None:
            return [f"{T}({wr}(w, in->{g.bits[0].field}));"]
        lines, terms = [], []
        const = sum(b.const << b.shift for b in g.bits if b.const is not None)
        if const:
            terms.append(hex(const))
        for b in g.bits:
            if b.field is not None:
                value = f"in->{b.field}"
                fits = b.width == c_bits(b.width)  # the C type cannot hold too large a value
            elif b.length_of is not None:
                value = f"in->{b.length_of}.len"
                fits = False  # size_t
            else:
                continue
            if not fits:
                lines += [f"    if ({value} > {hex((1 << b.width) - 1)})",
                          f"        return {P}_INVALID;"]
            terms.append(f"(({ct}){value} << {b.shift})" if b.shift else value)
        lines.append(join_wrapped(f"{T}({wr}(w, ({ct})(", terms or ["0"], " | ", ")));"))
        return lines

    def size(self, op, st: StructDecl) -> int | str:
        p = self.p
        match op:
            case Group():
                return op.size
            case Fixed():
                return op.elem * op.count
            case Cstr():
                return f"{p}_cstr_size(&in->{op.field})"
            case Rest() | Bytes():
                return f"in->{op.field}.len"
            case Sub():
                return f"{p}_{op.struct}_size(&in->{op.field})"
            case List():
                return f"{p}_{op.elem}_list_size(&in->{op.field})"
            case Dispatch():
                return f"{p}_{st.name}_{op.field}_size(in)"
        raise AssertionError(op)

    def size_body(self, st: StructDecl) -> list[str]:
        """Sum the field sizes; at each pad(), round the running total up."""
        lines, terms, have_n = [], [], False
        for op in st.ops:
            if not isinstance(op, Pad):
                terms.append(self.size(op, st))
                continue
            if not have_n:
                lines.append(sum_expr("    size_t n = ", fold(terms) or ["0"], ";"))
                have_n = True
            elif terms:
                lines.append(sum_expr("    n += ", fold(terms), ";"))
            lines.append(f"    n += {self.p}_pad_len(n, {op.align});")
            terms = []
        if have_n:
            return lines + [sum_expr("    return ", ["n", *fold(terms)], ";")]
        if all(isinstance(t, int) for t in terms):
            lines.append("    (void)in;")
        return lines + [sum_expr("    return ", fold(terms) or ["0"], ";")]

    # ---- switch, list and message functions ------------------------------------

    def c_dispatch(self, st: StructDecl, d: Dispatch) -> str:
        p, P = self.p, self.P
        s, fn = f"{p}_{st.name}", f"{p}_{st.name}_{d.field}"
        enum = f"{P}_{d.enum.upper()}"
        u = f"{d.field}.unknown"

        def cases(call: str, default: str) -> list[str]:
            lines = []
            for a in d.arms:
                lines += [f"    case {enum}_{t}:" for t in a.tags]
                lines.append(f"        return {call.format(m=a.member, s=a.struct)};")
            return lines + ["    default:", f"        return {default};", "    }", "}", ""]

        if d.fallback:
            defaults = (f"{p}_rd_rest(r, &out->{u})",
                        f"{p}_wr_raw(w, in->{u}.ptr, in->{u}.len)",
                        f"in->{u}.len")
        else:
            defaults = ("false; /* unknown tag */",
                        f"{P}_INVALID; /* unknown tag */",
                        "0; /* unknown tag: encode fails */")
        return "\n".join([
            sig(f"static bool {fn}_read", [f"struct {p}_rd *r", f"struct {s} *out"], " {"),
            f"    switch (out->{d.tag}) {{",
            *cases(f"{p}_{{s}}_read(r, &out->{d.field}.{{m}})", defaults[0]),
            sig(f"static {p}_status {fn}_write", [f"struct {p}_wr *w", f"const struct {s} *in"], " {"),
            f"    switch (in->{d.tag}) {{",
            *cases(f"{p}_{{s}}_write(w, &in->{d.field}.{{m}})", defaults[1]),
            sig(f"static size_t {fn}_size", [f"const struct {s} *in"], " {"),
            f"    switch (in->{d.tag}) {{",
            *cases(f"{p}_{{s}}_size(&in->{d.field}.{{m}})", defaults[2]),
        ])

    def c_list(self, st: StructDecl) -> str:
        p, s = self.p, f"{self.p}_{st.name}"
        return self.t(
            C_LIST, s=st.name, S=st.schema_name,
            list_read=sig(f"static bool {s}_list_read",
                          [f"struct {p}_rd *r", f"struct {s}_list *out"], " {"),
            list_write=sig(f"static {p}_status {s}_list_write",
                           [f"struct {p}_wr *w", f"const struct {s}_list *in"], " {"),
            list_size=sig(f"static size_t {s}_list_size", [f"const struct {s}_list *in"], " {"),
            iter_init=sig(f"void {s}_iter_init",
                          [f"struct {s}_iter *it", f"const struct {s}_list *list"], " {"),
            iter_next=sig(f"bool {s}_iter_next", [f"struct {s}_iter *it", f"struct {s} *out"], " {"))

    def c_message(self, st: StructDecl) -> str:
        p, P, s = self.p, self.P, f"{self.p}_{st.name}"
        decode_max = encode_max = ""
        if st.max_declared:
            m = f"{P}_{st.name.upper()}_MAX_SIZE"
            decode_max = f"    if (len > {m})\n        return {P}_INVALID;\n"
            encode_max = f"    if ({s}_size(in) > {m})\n        return {P}_INVALID;\n"
        return self.t(
            C_MESSAGE, s=s, decode_max=decode_max, encode_max=encode_max,
            decode=sig(f"{p}_status {s}_decode",
                       [f"struct {s} *out", "const uint8_t *buf", "size_t len"], " {"),
            encode=sig(f"{p}_status {s}_encode",
                       [f"const struct {s} *in", "uint8_t *buf", "size_t cap",
                        "size_t *written"], " {"),
            encoded_size=sig(f"size_t {s}_encoded_size", [f"const struct {s} *in"], " {"))


class FuzzEmitter(Emitter):
    """A libFuzzer harness for the generated code (<proto>_fuzz.c).

    Each input is used twice. As a packet: decode it, touch every view, and
    re-encode it. As a recipe: build_<struct> functions read choices (tags,
    list counts, lengths, values) from it to build a valid message, which
    must encode, decode and re-encode identically. The second half reaches
    deep structure (options, odd lengths, every arm) that random bytes
    rarely decode to, because the encoder computes lengths and padding.
    """

    def harness(self) -> str:
        p = self.p
        ops = [op for st in self.proto.structs for op in st.ops]
        self.uses_arena = any(isinstance(op, (Cstr, List)) for op in ops)
        self.structs = {st.name: st for st in self.proto.structs}
        self.enums = {e.name: e for e in self.proto.enums}
        out = [f"/* Generated from {self.proto.source} -- do not edit.",
               " *",
               f" * libFuzzer harness for the code in {p}_gen.c. Each input is used twice:",
               " * as a packet to decode (decode_<message>), and as a recipe for building",
               " * a valid message to encode (encode_<message>).",
               " *",
               " *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \\",
               f" *       {p}_fuzz.c {p}_gen.c -o {p}_fuzz",
               " */",
               "#include <stdlib.h>", "#include <string.h>", "",
               f'#include "{p}_gen.h"', "",
               self.t(F_COMMON)]
        body = []
        for st in self.proto.structs:
            body += self.walk(st)
        for st in self.proto.structs:
            body += self.build(st)
        for st in self.proto.structs:
            if st.is_message:
                body += self.decode_message(st, self.exact(st))
                body += self.encode_message(st)
        out += [self.helpers("\n".join(body)), *body]
        out += ["int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {",
                "    /* An exact-size heap copy, so ASan catches any overread. */",
                "    uint8_t *buf = malloc(size != 0 ? size : 1);",
                "    if (size != 0)",
                "        memcpy(buf, data, size);"]
        for st in self.proto.structs:
            if st.is_message:
                out += [f"    decode_{st.name}(buf, size);",
                        f"    encode_{st.name}(buf, size);"]
        out += ["    free(buf);", "    return 0;", "}"]
        return "\n".join(out) + "\n"

    def helpers(self, body: str) -> str:
        """Every F_HELPERS entry the harness calls, directly or indirectly."""
        texts = {k: self.t(v) for k, v in F_HELPERS}
        used, changed = set(), True
        while changed:
            search = body + "".join(texts[k] for k in used)
            new = {k for k in texts if k not in used and f"{k}(" in search}
            used |= new
            changed = bool(new)
        return "\n".join(texts[k] for k, _ in F_HELPERS if k in used)

    # ---- decode side -------------------------------------------------------

    def walk(self, st: StructDecl) -> list[str]:
        """Touch every view and list element of a decoded struct."""
        p, P = self.p, self.P
        body = []
        for op in st.ops:
            match op:
                case Cstr():
                    body.append(f"    walk_cstr(&v->{op.field});")
                case Rest() | Bytes():
                    body.append(f"    walk_bytes(&v->{op.field});")
                case Sub():
                    body.append(f"    walk_{op.struct}(&v->{op.field});")
                case List():
                    e = f"{p}_{op.elem}"
                    body += ["    {",
                             f"        struct {e}_iter it;",
                             f"        struct {e} item;",
                             f"        {e}_iter_init(&it, &v->{op.field});",
                             f"        while ({e}_iter_next(&it, &item))",
                             f"            walk_{op.elem}(&item);",
                             "    }"]
                case Dispatch():
                    enum = f"{P}_{op.enum.upper()}"
                    body.append(f"    switch (v->{op.tag}) {{")
                    for a in op.arms:
                        body += [f"    case {enum}_{t}:" for t in a.tags]
                        body += [f"        walk_{a.struct}(&v->{op.field}.{a.member});",
                                 "        break;"]
                    body.append("    default:")
                    if op.fallback:
                        body.append(f"        walk_bytes(&v->{op.field}.unknown);")
                    body += ["        break;", "    }"]
        return [f"static void walk_{st.name}(const struct {p}_{st.name} *v) {{",
                *(body or ["    (void)v;"]), "}", ""]

    def exact(self, st: StructDecl) -> bool:
        """True if decode -> encode reproduces the input byte for byte: no
        reserved field or padding is reachable from st."""
        seen = set()
        def ok(s: StructDecl) -> bool:
            if s.name in seen:
                return True
            seen.add(s.name)
            for op in s.ops:
                match op:
                    case Pad() | Fixed(field=None):
                        return False
                    case Group() if any(b.field is None and b.const is None and
                                        b.length_of is None for b in op.bits):
                        return False
                    case Sub() if not ok(self.structs[op.struct]):
                        return False
                    case List() if not ok(self.structs[op.elem]):
                        return False
                    case Dispatch() if not all(ok(self.structs[a.struct]) for a in op.arms):
                        return False
            return True
        return ok(st)

    def decode_message(self, st: StructDecl, exact: bool) -> list[str]:
        p, P, m = self.p, self.P, f"{self.p}_{st.name}"
        if exact:
            comment = ["/* Decode then encode must give back the input exactly. */"]
            check = ["    if (memcmp(a, buf, size) != 0)",
                     "        abort();"]
        else:
            comment = [f"/* {st.schema_name} has reserved fields or padding, which decode "
                       "ignores and",
                       " * encode writes as zeros, so the first encode may differ from the",
                       " * input there. It must still have the same length, decode again, and",
                       " * encode to identical bytes. */"]
            check = [f"    struct {m} again;",
                     f"    if ({m}_decode(&again, a, size) != {P}_OK)",
                     "        abort();",
                     f"    if ({m}_encode(&again, b, size, &n) != {P}_OK || n != size ||",
                     "        memcmp(a, b, size) != 0)",
                     "        abort();"]
        return [*comment,
                f"static void decode_{st.name}(const uint8_t *buf, size_t size) {{",
                f"    struct {m} msg;",
                f"    if ({m}_decode(&msg, buf, size) != {P}_OK)",
                "        return;",
                f"    walk_{st.name}(&msg);",
                f"    if ({m}_encoded_size(&msg) != size)",
                "        abort();",
                "    uint8_t *a = malloc(size != 0 ? size : 1);",
                "    uint8_t *b = malloc(size != 0 ? size : 1);",
                "    size_t n = 0;",
                f"    if ({m}_encode(&msg, a, size, &n) != {P}_OK || n != size)",
                "        abort();",
                f"    if (size != 0 && {m}_encode(&msg, b, size - 1, &n) != {P}_NOSPACE)",
                "        abort();",
                *check,
                "    free(a);",
                "    free(b);",
                "}", ""]

    # ---- encode side -------------------------------------------------------

    def build(self, st: StructDecl) -> list[str]:
        """Fill a struct with valid values chosen by the input."""
        p, P = self.p, self.P
        body = []
        for op in st.ops:
            match op:
                case Group():
                    for b in op.bits:
                        if b.field is not None:
                            mask = hex((1 << b.width) - 1)
                            body.append(f"    v->{b.field} = ({self.member_type(b)})"
                                        f"(take_u32(in) & {mask});")
                case Fixed(field=None):
                    pass
                case Fixed(elem=1):
                    body.append(f"    take_raw(in, v->{op.field}, {op.count});")
                case Fixed():
                    body += [f"    for (size_t i = 0; i < {op.count}; i++)",
                             f"        v->{op.field}[i] = (uint{8 * op.elem}_t)take_u32(in);"]
                case Cstr():
                    body.append(f"    take_cstr(in, &v->{op.field});")
                case Rest():
                    body.append(f"    take_bytes(in, &v->{op.field}, SIZE_MAX);")
                case Bytes():
                    body.append(f"    take_bytes(in, &v->{op.field}, "
                                f"{hex((1 << op.len_width) - 1)});")
                case Sub():
                    body.append(f"    build_{op.struct}(in, &v->{op.field});")
                case List():
                    e = f"{p}_{op.elem}"
                    body += ["    {",
                             "        size_t n = take_u8(in) % 4;",
                             f"        struct {e} *items = arena_alloc(n * sizeof *items);",
                             "        if (items == NULL)",
                             "            n = 0;",
                             "        for (size_t i = 0; i < n; i++)",
                             f"            build_{op.elem}(in, &items[i]);",
                             f"        v->{op.field} = (struct {e}_list){{ .items = items, "
                             ".count = n };",
                             "    }"]
                case Dispatch():
                    body += self.build_dispatch(op)
        return [f"static void build_{st.name}(struct src *in, struct {p}_{st.name} *v) {{",
                *(body or ["    (void)in;", "    (void)v;"]), "}", ""]

    def build_dispatch(self, d: Dispatch) -> list[str]:
        P, enum = self.P, self.enums[d.enum]
        prefix = f"{P}_{d.enum.upper()}"
        values = dict(enum.variants)
        cases = [(f"{prefix}_{t}", a) for a in d.arms for t in a.tags]
        unknown = None
        if d.fallback:  # a value no arm handles, if the enum's width has one
            listed = {values[t] for a in d.arms for t in a.tags}
            unknown = next((v for v in range(1 << enum.width) if v not in listed), None)
        n = len(cases) + (unknown is not None)
        lines = [f"    switch (take_u8(in) % {n}) {{"]
        for i, (tag, a) in enumerate(cases):
            lines += [f"    case {i}:",
                      f"        v->{d.tag} = {tag};",
                      f"        build_{a.struct}(in, &v->{d.field}.{a.member});",
                      "        break;"]
        if unknown is not None:
            lines += ["    default:",
                      f"        v->{d.tag} = (enum {self.p}_{d.enum}){unknown};  /* no arm */",
                      f"        take_bytes(in, &v->{d.field}.unknown, SIZE_MAX);",
                      "        break;"]
        else:  # the last case catches everything, so -Wswitch needs no default
            lines = [l if l != f"    case {len(cases) - 1}:" else "    default:" for l in lines]
        return lines + ["    }"]

    def encode_message(self, st: StructDecl) -> list[str]:
        p, P, m = self.p, self.P, f"{self.p}_{st.name}"
        over_max = []
        if st.max_declared:
            over_max = [f"    if (size > {P}_{st.name.upper()}_MAX_SIZE) {{",
                        f"        if ({m}_encode(&msg, NULL, 0, &n) != {P}_INVALID)",
                        "            abort();",
                        "        return;",
                        "    }"]
        return [f"/* Build a valid {st.schema_name} from the input, then encode it: it must take",
                " * exactly encoded_size bytes, decode, and encode to identical bytes. */",
                f"static void encode_{st.name}(const uint8_t *data, size_t len) {{",
                "    struct src in = { data, data + len };",
                f"    struct {m} msg = {{}};",
                *(["    arena_used = 0;"] if self.uses_arena else []),
                f"    build_{st.name}(&in, &msg);",
                f"    size_t size = {m}_encoded_size(&msg);",
                "    size_t n = 0;",
                *over_max,
                "    uint8_t *a = malloc(size != 0 ? size : 1);",
                "    uint8_t *b = malloc(size != 0 ? size : 1);",
                f"    if ({m}_encode(&msg, a, size, &n) != {P}_OK || n != size)",
                "        abort();",
                f"    struct {m} back;",
                f"    if ({m}_decode(&back, a, size) != {P}_OK)",
                "        abort();",
                f"    if ({m}_encode(&back, b, size, &n) != {P}_OK || n != size ||",
                "        memcmp(a, b, size) != 0)",
                "        abort();",
                "    free(a);",
                "    free(b);",
                "}", ""]


def emit(proto: Proto) -> tuple[str, str]:
    e = Emitter(proto)
    return e.header(), e.source()


def emit_fuzz(proto: Proto) -> str:
    return FuzzEmitter(proto).harness()


# ---- fuzz harness templates ------------------------------------------------------

F_COMMON = """\
/* Recipe input: choices are read from [p, end); past the end, they are 0. */
struct src {
    const uint8_t *p;
    const uint8_t *end;
};
"""

# Emitted in this order, each only if the harness calls it.
F_HELPERS = [
    ("take_u8", """\
static uint8_t take_u8(struct src *in) {
    return in->p < in->end ? *in->p++ : 0;
}
"""),
    ("take_u32", """\
static uint32_t take_u32(struct src *in) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v = v << 8 | take_u8(in);
    return v;
}
"""),
    ("take_raw", """\
static void take_raw(struct src *in, uint8_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = take_u8(in);
}
"""),
    ("arena_alloc", """\
/* Storage for list items and strings built from one recipe. */
static alignas(max_align_t) uint8_t arena[1 << 16];
static size_t arena_used;

static void *arena_alloc(size_t n) {
    size_t a = alignof(max_align_t);
    size_t at = (arena_used + a - 1) / a * a;
    if (n == 0 || n > sizeof arena - at)
        return NULL;
    arena_used = at + n;
    return arena + at;
}
"""),
    ("walk_bytes", """\
static volatile uint8_t sink;

static void walk_bytes(const struct ${p}_bytes *v) {
    if (v->len != 0)
        sink = v->ptr[v->len - 1];
}
"""),
    ("take_bytes", """\
/* A view into the recipe itself: up to max bytes, as many as remain. */
static void take_bytes(struct src *in, struct ${p}_bytes *v, size_t max) {
    size_t n = (size_t)take_u8(in) << 8 | take_u8(in);
    size_t left = (size_t)(in->end - in->p);
    n = n % (max < SIZE_MAX ? max + 1 : SIZE_MAX);
    v->len = n < left ? n : left;
    v->ptr = in->p;
    in->p += v->len;
}
"""),
    ("walk_cstr", """\
static void walk_cstr(const struct ${p}_cstr *v) {
    if (v->ptr[v->len] != '\\0')
        abort();
}
"""),
    ("take_cstr", """\
/* A short string without NULs, copied into the arena. */
static void take_cstr(struct src *in, struct ${p}_cstr *v) {
    size_t n = take_u8(in) % 16;
    char *s = arena_alloc(n + 1);
    if (s == NULL)
        n = 0, s = "";
    for (size_t i = 0; i < n; i++) {
        uint8_t c = take_u8(in);
        s[i] = (char)(c != 0 ? c : 'x');
    }
    v->ptr = s;
    v->len = n;
}
"""),
]


# ---- templates ----------------------------------------------------------------
# $p/$P: prefix in lower/upper case; $s/$S: list element (C/schema name).
# Function signatures that depend on name lengths are passed in pre-wrapped.

H_STATUS = """\
typedef enum {
    ${P}_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    ${P}_INVALID,
    /* encode: output buffer too small */
    ${P}_NOSPACE,
} ${p}_status;
"""

H_CSTR = r"""/* cstr: points into the buffer. After a successful decode, ptr[len] == '\0',
 * so ptr is also usable as a C string. len excludes the terminator. */
struct ${p}_cstr {
    const char *ptr;
    size_t len;
};
"""

H_BYTES = """\
/* u8[..] and u8[len]: points into the buffer. */
struct ${p}_bytes {
    const uint8_t *ptr;
    size_t len;
};
"""

H_LIST = """\
/* ${S}[..]
 * decode: raw/raw_len cover the validated wire bytes; walk them with
 *         ${p}_${s}_iter. items is NULL.
 * encode: if items is non-NULL, the count entries are written;
 *         otherwise raw is copied as-is (so decode -> encode round-trips). */
struct ${p}_${s}_list {
    const uint8_t *raw;
    size_t raw_len;
    const struct ${p}_${s} *items;
    size_t count;
};

struct ${p}_${s}_iter {
    const uint8_t *pos;
    const uint8_t *end;
};
"""

H_CSTR_MACRO = """\
/* Initializer for a cstr from a string literal (encode side). */
#define ${P}_CSTR(s) { .ptr = (s), .len = sizeof(s) - 1 }
"""

C_CURSORS = """\
/* Read cursor over [p, end). Every read checks bounds first. */
struct ${p}_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct ${p}_wr {
    uint8_t *p;
    uint8_t *end;
};
"""

# Emitted in this order, each only if the generated code calls it.
PRIMITIVES = [
    ("rd_left", """\
static size_t ${p}_rd_left(const struct ${p}_rd *r) {
    return (size_t)(r->end - r->p);
}
"""),
    ("rd_u8", """\
static bool ${p}_rd_u8(struct ${p}_rd *r, uint8_t *v) {
    if (${p}_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}
"""),
    ("rd_u16", """\
static bool ${p}_rd_u16(struct ${p}_rd *r, uint16_t *v) {
    if (${p}_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}
"""),
    ("rd_u24", """\
static bool ${p}_rd_u24(struct ${p}_rd *r, uint32_t *v) {
    if (${p}_rd_left(r) < 3)
        return false;
    *v = (uint32_t)r->p[0] << 16 | (uint32_t)r->p[1] << 8 | r->p[2];
    r->p += 3;
    return true;
}
"""),
    ("rd_u32", """\
static bool ${p}_rd_u32(struct ${p}_rd *r, uint32_t *v) {
    if (${p}_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}
"""),
    ("rd_skip", """\
static bool ${p}_rd_skip(struct ${p}_rd *r, size_t n) {
    if (${p}_rd_left(r) < n)
        return false;
    r->p += n;
    return true;
}
"""),
    ("rd_raw", """\
static bool ${p}_rd_raw(struct ${p}_rd *r, uint8_t *dst, size_t n) {
    if (${p}_rd_left(r) < n)
        return false;
    memcpy(dst, r->p, n);
    r->p += n;
    return true;
}
"""),
    ("rd_cstr", """\
static bool ${p}_rd_cstr(struct ${p}_rd *r, struct ${p}_cstr *v) {
    if (r->p == r->end)
        return false;
    const uint8_t *nul = memchr(r->p, 0, ${p}_rd_left(r));
    if (nul == NULL)
        return false;
    v->ptr = (const char *)r->p;
    v->len = (size_t)(nul - r->p);
    r->p = nul + 1;
    return true;
}
"""),
    ("rd_rest", """\
static bool ${p}_rd_rest(struct ${p}_rd *r, struct ${p}_bytes *v) {
    v->ptr = r->p;
    v->len = ${p}_rd_left(r);
    r->p = r->end;
    return true;
}
"""),
    ("rd_bytes", """\
static bool ${p}_rd_bytes(struct ${p}_rd *r, struct ${p}_bytes *v, size_t n) {
    if (${p}_rd_left(r) < n)
        return false;
    v->ptr = r->p;
    v->len = n;
    r->p += n;
    return true;
}
"""),
    ("wr_left", """\
static size_t ${p}_wr_left(const struct ${p}_wr *w) {
    return (size_t)(w->end - w->p);
}
"""),
    ("wr_u8", """\
static ${p}_status ${p}_wr_u8(struct ${p}_wr *w, uint8_t v) {
    if (${p}_wr_left(w) < 1)
        return ${P}_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return ${P}_OK;
}
"""),
    ("wr_u16", """\
static ${p}_status ${p}_wr_u16(struct ${p}_wr *w, uint16_t v) {
    if (${p}_wr_left(w) < 2)
        return ${P}_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return ${P}_OK;
}
"""),
    ("wr_u24", """\
static ${p}_status ${p}_wr_u24(struct ${p}_wr *w, uint32_t v) {
    if (${p}_wr_left(w) < 3)
        return ${P}_NOSPACE;
    w->p[0] = (uint8_t)(v >> 16);
    w->p[1] = (uint8_t)(v >> 8);
    w->p[2] = (uint8_t)v;
    w->p += 3;
    return ${P}_OK;
}
"""),
    ("wr_u32", """\
static ${p}_status ${p}_wr_u32(struct ${p}_wr *w, uint32_t v) {
    if (${p}_wr_left(w) < 4)
        return ${P}_NOSPACE;
    w->p[0] = (uint8_t)(v >> 24);
    w->p[1] = (uint8_t)(v >> 16);
    w->p[2] = (uint8_t)(v >> 8);
    w->p[3] = (uint8_t)v;
    w->p += 4;
    return ${P}_OK;
}
"""),
    ("wr_raw", """\
static ${p}_status ${p}_wr_raw(struct ${p}_wr *w, const void *src, size_t n) {
    if (${p}_wr_left(w) < n)
        return ${P}_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return ${P}_OK;
}
"""),
    ("wr_zero", """\
static ${p}_status ${p}_wr_zero(struct ${p}_wr *w, size_t n) {
    if (${p}_wr_left(w) < n)
        return ${P}_NOSPACE;
    if (n != 0)
        memset(w->p, 0, n);
    w->p += n;
    return ${P}_OK;
}
"""),
    ("wr_cstr", """\
static ${p}_status ${p}_wr_cstr(struct ${p}_wr *w, const struct ${p}_cstr *v) {
    if (v->len != 0 && memchr(v->ptr, 0, v->len) != NULL)
        return ${P}_INVALID;
    if (${p}_wr_left(w) < v->len + 1)
        return ${P}_NOSPACE;
    ${p}_wr_raw(w, v->ptr, v->len);
    w->p[0] = 0;
    w->p += 1;
    return ${P}_OK;
}
"""),
    ("cstr_size", """\
static size_t ${p}_cstr_size(const struct ${p}_cstr *v) {
    return v->len + 1;
}
"""),
    ("pad_len", """\
/* Bytes needed after offset n to reach a multiple of align. */
static size_t ${p}_pad_len(size_t n, size_t align) {
    return (align - n % align) % align;
}
"""),
    ("CHECK", """\
#define ${P}_CHECK(cond) \\
    do { \\
        if (!(cond)) \\
            return false; \\
    } while (0)
"""),
    ("TRY", """\
#define ${P}_TRY(expr) \\
    do { \\
        ${p}_status st_ = (expr); \\
        if (st_ != ${P}_OK) \\
            return st_; \\
    } while (0)
"""),
]

C_LIST = """\
/* Consumes the rest of r. Fails unless it is a whole number of ${S}s. */
${list_read}
    const uint8_t *start = r->p;
    struct ${p}_${s} tmp;
    while (r->p != r->end)
        if (!${p}_${s}_read(r, &tmp))
            return false;
    out->raw = start;
    out->raw_len = (size_t)(r->p - start);
    out->items = NULL;
    out->count = 0;
    return true;
}

${list_write}
    if (in->items != NULL) {
        for (size_t i = 0; i < in->count; i++)
            ${P}_TRY(${p}_${s}_write(w, &in->items[i]));
        return ${P}_OK;
    }
    if (in->raw_len == 0)
        return ${P}_OK;
    /* raw may have been filled in by hand: encode never emits bytes that
     * decode would reject. */
    struct ${p}_rd r = { in->raw, in->raw + in->raw_len };
    struct ${p}_${s}_list check;
    if (!${p}_${s}_list_read(&r, &check))
        return ${P}_INVALID;
    return ${p}_wr_raw(w, in->raw, in->raw_len);
}

${list_size}
    if (in->items == NULL)
        return in->raw_len;
    size_t n = 0;
    for (size_t i = 0; i < in->count; i++)
        n += ${p}_${s}_size(&in->items[i]);
    return n;
}

${iter_init}
    it->pos = list->raw;
    it->end = list->raw_len != 0 ? list->raw + list->raw_len : list->raw;
}

${iter_next}
    if (it->pos == it->end)
        return false;
    struct ${p}_rd r = { it->pos, it->end };
    if (!${p}_${s}_read(&r, out))
        return false;
    it->pos = r.p;
    return true;
}
"""

C_MESSAGE = """\
${decode}
${decode_max}    struct ${p}_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!${s}_read(&r, out) || r.p != r.end)
        return ${P}_INVALID;
    return ${P}_OK;
}

${encode}
${encode_max}    struct ${p}_wr w = { buf, cap != 0 ? buf + cap : buf };
    ${P}_TRY(${s}_write(&w, in));
    *written = (size_t)(w.p - buf);
    return ${P}_OK;
}

${encoded_size}
    return ${s}_size(in);
}
"""
