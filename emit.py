"""Emit: IR -> <proto>_gen.h and <proto>_gen.c."""
from string import Template

from ir import Proto, StructDecl, EnumDecl, Int, Cstr, Rest, Sub, List, Dispatch

WIDTH = 79
C_INT = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t"}


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


def ret(terms: list[str], op: str) -> str:
    """`return a op b op c;`, one term per line if it does not fit."""
    one = f"    return {f' {op} '.join(terms)};"
    if len(one) <= WIDTH:
        return one
    pad = " " * (11 - len(op) - 1)  # operands line up under the first term
    return "\n".join([f"    return {terms[0]}"] + [f"{pad}{op} {t}" for t in terms[1:]]) + ";"


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


class Emitter:
    def __init__(self, proto: Proto):
        self.proto = proto
        self.p = proto.name.lower()
        self.P = proto.name.upper()
        ops = [op for st in proto.structs for op in st.ops]
        self.uses_cstr = any(isinstance(op, Cstr) for op in ops)
        self.uses_rest = any(isinstance(op, Rest) for op in ops)
        self.uses_list = any(isinstance(op, List) for op in ops)
        self.int_sizes = sorted({op.size for op in ops if isinstance(op, Int)})

    def t(self, text: str, **kw) -> str:
        return Template(text).substitute(p=self.p, P=self.P, **kw)

    # ---- header ----------------------------------------------------------

    def header(self) -> str:
        guard = f"{self.P}_GEN_H"
        out = [f"/* Generated from {self.proto.source} -- do not edit. */",
               f"#ifndef {guard}", f"#define {guard}", "",
               "#include <stdbool.h>", "#include <stddef.h>", "#include <stdint.h>", "",
               self.t(H_STATUS)]
        out += [self.h_enum(e) for e in self.proto.enums]
        if self.uses_cstr:
            out.append(self.t(H_CSTR))
        if self.uses_rest:
            out.append(self.t(H_BYTES))
        for st in self.proto.structs:
            out.append(self.h_struct(st))
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
        out.append(f"#endif")
        return "\n".join(out) + "\n"

    def h_enum(self, e: EnumDecl) -> str:
        rows = [(f"    {self.P}_{e.name.upper()}_{v}", f" {n},") for v, n in e.variants]
        return "\n".join([f"/* {e.decl} */",
                          f"enum {self.p}_{e.name} : {C_INT[e.size]} {{",
                          *table(rows, " ="), "};", ""])

    def c_type(self, op) -> str:
        p = self.p
        match op:
            case Int(enum=None):
                return C_INT[op.size]
            case Int():
                return f"enum {p}_{op.enum}"
            case Cstr():
                return f"struct {p}_cstr"
            case Rest():
                return f"struct {p}_bytes"
            case Sub():
                return f"struct {p}_{op.struct}"
            case List():
                return f"struct {p}_{op.elem}_list"
        raise AssertionError(op)

    def h_struct(self, st: StructDecl) -> str:
        tags = {op.tag: op.field for op in st.ops if isinstance(op, Dispatch)}
        out = [decl_comment(st.decl), f"struct {self.p}_{st.name} {{"]
        for op in st.ops:
            if isinstance(op, Dispatch):
                rows = [(f"        struct {self.p}_{a.struct}", f" {a.member};") for a in op.arms]
                rows = table(rows, "")
                rows = [(r, f"  /* {', '.join(a.tags)} */") for r, a in zip(rows, op.arms)]
                out += ["    union {", *table(rows, ""), f"    }} {op.field};"]
            elif op.field in tags:
                out.append(f"    {self.c_type(op)} {op.field};  /* selects {tags[op.field]} */")
            else:
                out.append(f"    {self.c_type(op)} {op.field};")
        out += ["};", ""]
        return "\n".join(out)

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
        out = [f"/* Generated from {self.proto.source} -- do not edit. */",
               f'#include "{self.p}_gen.h"', "", "#include <string.h>", "",
               rule("wire primitives"), "", self.primitives()]
        for st in self.proto.structs:
            kind = "message" if st.is_message else "struct"
            out += [rule(f"{kind} {st.schema_name}"), "", self.c_struct(st)]
            if st.is_list_elem:
                out += [rule(f"{st.schema_name}[..]"), "",
                        self.c_list(st)]
        return "\n".join(out).rstrip("\n") + "\n"

    def primitives(self) -> str:
        parts = [C_CURSORS]
        parts += [C_RD_INT[n] for n in self.int_sizes]
        if self.uses_cstr:
            parts.append(C_RD_CSTR)
        if self.uses_rest:
            parts.append(C_RD_REST)
        parts.append(C_WR_LEFT)
        parts += [C_WR_INT[n] for n in self.int_sizes]
        if self.uses_cstr or self.uses_rest or self.uses_list:
            parts.append(C_WR_RAW)
        if self.uses_cstr:
            parts += [C_WR_CSTR, C_CSTR_SIZE]
        parts.append(C_TRY)
        return "\n".join(self.t(x) for x in parts)

    def read(self, op, st: StructDecl) -> str:
        p = self.p
        match op:
            case Int():
                return f"{p}_rd_u{8 * op.size}(r, &out->{op.field})"
            case Cstr():
                return f"{p}_rd_cstr(r, &out->{op.field})"
            case Rest():
                return f"{p}_rd_rest(r, &out->{op.field})"
            case Sub():
                return f"{p}_{op.struct}_read(r, &out->{op.field})"
            case List():
                return f"{p}_{op.elem}_list_read(r, &out->{op.field})"
            case Dispatch():
                return f"{p}_{st.name}_{op.field}_read(r, out)"
        raise AssertionError(op)

    def write(self, op, st: StructDecl) -> str:
        p = self.p
        match op:
            case Int():
                return f"{p}_wr_u{8 * op.size}(w, in->{op.field})"
            case Cstr():
                return f"{p}_wr_cstr(w, &in->{op.field})"
            case Rest():
                return f"{p}_wr_raw(w, in->{op.field}.ptr, in->{op.field}.len)"
            case Sub():
                return f"{p}_{op.struct}_write(w, &in->{op.field})"
            case List():
                return f"{p}_{op.elem}_list_write(w, &in->{op.field})"
            case Dispatch():
                return f"{p}_{st.name}_{op.field}_write(w, in)"
        raise AssertionError(op)

    def size(self, op, st: StructDecl) -> int | str:
        p = self.p
        match op:
            case Int():
                return op.size
            case Cstr():
                return f"{p}_cstr_size(&in->{op.field})"
            case Rest():
                return f"in->{op.field}.len"
            case Sub():
                return f"{p}_{op.struct}_size(&in->{op.field})"
            case List():
                return f"{p}_{op.elem}_list_size(&in->{op.field})"
            case Dispatch():
                return f"{p}_{st.name}_{op.field}_size(in)"
        raise AssertionError(op)

    def c_struct(self, st: StructDecl) -> str:
        p, P, s = self.p, self.P, f"{self.p}_{st.name}"
        out = []
        for op in st.ops:
            if isinstance(op, Dispatch):
                out.append(self.c_dispatch(st, op))

        out += [sig(f"static bool {s}_read", [f"struct {p}_rd *r", f"struct {s} *out"], " {"),
                ret([self.read(op, st) for op in st.ops], "&&"), "}", ""]

        out.append(sig(f"static {p}_status {s}_write",
                       [f"struct {p}_wr *w", f"const struct {s} *in"], " {"))
        out += [f"    {P}_TRY({self.write(op, st)});" for op in st.ops]
        out += [f"    return {P}_OK;", "}", ""]

        sizes = [self.size(op, st) for op in st.ops]
        fixed = sum(x for x in sizes if isinstance(x, int))
        terms = ([str(fixed)] if fixed else []) + [x for x in sizes if isinstance(x, str)]
        out.append(sig(f"static size_t {s}_size", [f"const struct {s} *in"], " {"))
        if len(terms) == 1 and fixed:
            out.append("    (void)in;")
        out += [ret(terms or ["0"], "+"), "}", ""]

        if st.is_message:
            out.append(self.c_message(st))
        return "\n".join(out)

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
        p, s = self.p, f"{self.p}_{st.name}"
        return self.t(
            C_MESSAGE, s=s,
            decode=sig(f"{p}_status {s}_decode",
                       [f"struct {s} *out", "const uint8_t *buf", "size_t len"], " {"),
            encode=sig(f"{p}_status {s}_encode",
                       [f"const struct {s} *in", "uint8_t *buf", "size_t cap",
                        "size_t *written"], " {"),
            encoded_size=sig(f"size_t {s}_encoded_size", [f"const struct {s} *in"], " {"))

    def c_dispatch(self, st: StructDecl, d: Dispatch) -> str:
        p, P = self.p, self.P
        s, fn = f"{p}_{st.name}", f"{p}_{st.name}_{d.field}"
        enum = f"{P}_{d.enum.upper()}"

        def cases(call: str, default: str, why: str) -> list[str]:
            lines = []
            for a in d.arms:
                lines += [f"    case {enum}_{t}:" for t in a.tags]
                lines.append(f"        return {call.format(m=a.member, s=a.struct)};")
            return lines + ["    default:", f"        return {default}; /* {why} */",
                            "    }", "}", ""]

        return "\n".join([
            sig(f"static bool {fn}_read", [f"struct {p}_rd *r", f"struct {s} *out"], " {"),
            f"    switch (out->{d.tag}) {{",
            *cases(f"{p}_{{s}}_read(r, &out->{d.field}.{{m}})", "false", "unknown tag"),
            sig(f"static {p}_status {fn}_write", [f"struct {p}_wr *w", f"const struct {s} *in"], " {"),
            f"    switch (in->{d.tag}) {{",
            *cases(f"{p}_{{s}}_write(w, &in->{d.field}.{{m}})", f"{P}_INVALID", "unknown tag"),
            sig(f"static size_t {fn}_size", [f"const struct {s} *in"], " {"),
            f"    switch (in->{d.tag}) {{",
            *cases(f"{p}_{{s}}_size(&in->{d.field}.{{m}})", "0", "unknown tag: encode fails"),
        ])


def emit(proto: Proto) -> tuple[str, str]:
    e = Emitter(proto)
    return e.header(), e.source()


# ---- templates ----------------------------------------------------------------
# $p/$P: prefix in lower/upper case; $s/$S: list element (C/schema name).
# Function signatures that depend on name lengths are passed in pre-wrapped.

H_STATUS = """\
typedef enum {
    ${P}_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, NUL in cstr) */
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
/* u8[..]: points into the buffer. */
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

static size_t ${p}_rd_left(const struct ${p}_rd *r) {
    return (size_t)(r->end - r->p);
}
"""

C_RD_INT = {
    1: """\
static bool ${p}_rd_u8(struct ${p}_rd *r, uint8_t *v) {
    if (${p}_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}
""",
    2: """\
static bool ${p}_rd_u16(struct ${p}_rd *r, uint16_t *v) {
    if (${p}_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}
""",
    4: """\
static bool ${p}_rd_u32(struct ${p}_rd *r, uint32_t *v) {
    if (${p}_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}
""",
}

C_RD_CSTR = """\
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
"""

C_RD_REST = """\
static bool ${p}_rd_rest(struct ${p}_rd *r, struct ${p}_bytes *v) {
    v->ptr = r->p;
    v->len = ${p}_rd_left(r);
    r->p = r->end;
    return true;
}
"""

C_WR_LEFT = """\
static size_t ${p}_wr_left(const struct ${p}_wr *w) {
    return (size_t)(w->end - w->p);
}
"""

C_WR_INT = {
    1: """\
static ${p}_status ${p}_wr_u8(struct ${p}_wr *w, uint8_t v) {
    if (${p}_wr_left(w) < 1)
        return ${P}_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return ${P}_OK;
}
""",
    2: """\
static ${p}_status ${p}_wr_u16(struct ${p}_wr *w, uint16_t v) {
    if (${p}_wr_left(w) < 2)
        return ${P}_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return ${P}_OK;
}
""",
    4: """\
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
""",
}

C_WR_RAW = """\
static ${p}_status ${p}_wr_raw(struct ${p}_wr *w, const void *src, size_t n) {
    if (${p}_wr_left(w) < n)
        return ${P}_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return ${P}_OK;
}
"""

C_WR_CSTR = """\
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
"""

C_CSTR_SIZE = """\
static size_t ${p}_cstr_size(const struct ${p}_cstr *v) {
    return v->len + 1;
}
"""

C_TRY = """\
#define ${P}_TRY(expr) \\
    do { \\
        ${p}_status st_ = (expr); \\
        if (st_ != ${P}_OK) \\
            return st_; \\
    } while (0)
"""

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
    struct ${p}_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!${s}_read(&r, out) || r.p != r.end)
        return ${P}_INVALID;
    return ${P}_OK;
}

${encode}
    struct ${p}_wr w = { buf, cap != 0 ? buf + cap : buf };
    ${P}_TRY(${s}_write(&w, in));
    *written = (size_t)(w.p - buf);
    return ${P}_OK;
}

${encoded_size}
    return ${s}_size(in);
}
"""
