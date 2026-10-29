"""Checker tests. Run with: python3 -m unittest discover tests"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from check import SchemaError, check, size_bounds, snake  # noqa: E402
from schema import ParseError, parse  # noqa: E402

HEADER = "protocol p;\n"
TAGGED = """
enum Kind : u8 { A = 1, B = 2, C = 3 }
message M { kind: Kind; body: switch (kind) { %s }; }
struct X { a: u8; }
struct Y { b: u16; }
"""


def errors(text: str) -> list[tuple[int, str]]:
    try:
        check(parse(text))
    except SchemaError as e:
        return e.errors
    return []


class Valid(unittest.TestCase):
    def test_tftp(self):
        self.assertEqual(errors((ROOT / "examples/tftp/tftp.schema").read_text()), [])

    def test_pcp_like(self):
        self.assertEqual(errors(HEADER + """
            enum Op : u8 { MAP = 1, PEER = 2 }
            message Request {
                version: u8;
                op: Op;
                reserved: u16;
                lifetime: u32;
                client: u8[16];
                body: switch (op) { MAP => Map, _ => u8[..], };
            }
            struct Map { nonce: u8[12]; port: u16; options: Opt[..]; }
            struct Opt { code: u8; reserved: u8; len: u16; data: u8[len] pad 4; }
        """), [])

    def test_stun_like(self):
        self.assertEqual(errors(HEADER + """
            message M { zero: u2 = 0; type: u14; len: u16; attrs: Attr[len]; }
            struct Attr { type: u16; len: u16; value: u8[len] pad 4; }
        """), [])

    def test_signed(self):
        self.assertEqual(errors(HEADER + "message M { a: i8; b: i3; c: u5; d: i32; }"), [])

    def test_sized_list_not_last(self):
        self.assertEqual(errors(HEADER + """
            message M { n: u8; items: S[n]; after: u8; }
            struct S { a: u8; }
        """), [])

    def test_open_struct_last_in_struct_last(self):
        self.assertEqual(errors(HEADER + """
            message M { a: u8; tail: Tail; }
            struct Tail { b: u8; rest: u8[..]; }
        """), [])


class Invalid(unittest.TestCase):
    def assertError(self, text: str, fragment: str, line: int | None = None):
        errs = errors(text)
        hits = [(l, m) for l, m in errs if fragment in m]
        self.assertTrue(hits, f"expected an error containing {fragment!r}, got {errs}")
        if line is not None:
            self.assertIn(line, [l for l, _ in hits], errs)

    # top level
    def test_missing_protocol(self):
        self.assertError("message M { a: u8; }", "missing 'protocol")

    def test_little_endian(self):
        self.assertError(HEADER + "endian little; message M { a: u8; }", "not supported")

    def test_no_message(self):
        self.assertError(HEADER + "struct S { a: u8; }", "no message")

    def test_duplicate_decl(self):
        self.assertError(HEADER + "message M { a: u8; }\nstruct M { b: u8; }", "declared twice", 3)

    def test_builtin_name(self):
        self.assertError(HEADER + "message M { a: u8; } struct cstr { a: u8; }", "built-in")

    # enums
    def test_enum_base(self):
        self.assertError(HEADER + "enum E : cstr { A = 1 } message M { a: u8; }", "base type")

    def test_enum_value_range(self):
        self.assertError(HEADER + "enum E : u8 { A = 256 } message M { a: u8; }", "does not fit")

    def test_enum_duplicate_variant(self):
        self.assertError(HEADER + "enum E : u8 { A = 1, A = 2 } message M { a: u8; }",
                         "declared twice")

    def test_enum_duplicate_value(self):
        self.assertError(HEADER + "enum E : u8 { A = 1, B = 1 } message M { a: u8; }",
                         "same value")

    # fields
    def test_unknown_type(self):
        self.assertError(HEADER + "message M {\n a: Nope; }", "unknown type 'Nope'", 3)

    def test_message_as_type(self):
        self.assertError(HEADER + "message M { a: u8; } message N { m: M; }", "is a message")

    def test_duplicate_field(self):
        self.assertError(HEADER + "message M { a: u8; a: u16; }", "declared twice")

    def test_c_keyword_field(self):
        self.assertError(HEADER + "message M { int: u8; }", "C keyword")

    def test_empty_struct(self):
        self.assertError(HEADER + "message M { a: u8; } struct S { }", "empty")

    def test_zero_pad(self):
        self.assertError(HEADER + "message M { n: u8; a: u8[n] pad 0; }", "pad 0")

    def test_pad_not_sized(self):
        for t in ("u8[..]", "u8[4]", "u16", "cstr"):
            self.assertError(HEADER + f"message M {{ a: u8; b: {t} pad 4; }}",
                             "only a u8[len] field can be padded")

    def test_pad_sized_list(self):
        self.assertError(HEADER + "message M { n: u8; a: S[n] pad 4; } struct S { b: u8; }",
                         "only a u8[len] field can be padded")

    # arrays
    def test_zero_length_array(self):
        self.assertError(HEADER + "message M { a: u8[0]; }", "at least 1")

    def test_array_of_cstr(self):
        self.assertError(HEADER + "message M { a: cstr[..]; }", "array elements must be")

    def test_fixed_array_of_structs(self):
        self.assertError(HEADER + "message M { a: S[3]; } struct S { b: u8; }",
                         "must be 'S[..]' or sized")

    def test_open_sized_list_element(self):
        self.assertError(HEADER + "message M { n: u8; a: S[n]; } struct S { b: u8[..]; }",
                         "cannot be a list element")

    def test_sized_list_length_field_later(self):
        self.assertError(HEADER + "message M { a: S[n]; n: u8; } struct S { b: u8; }",
                         "declared earlier")

    def test_sized_list_c_name_collision(self):
        self.assertError(HEADER + "message M { n: u8; a: S[n]; } struct S { b: u8; }"
                         " struct SList { c: u8; }", "<proto>_s_list")

    def test_open_list_element(self):
        self.assertError(HEADER + "message M { a: S[..]; } struct S { b: u8[..]; }",
                         "cannot be a list element")

    def test_rest_not_last(self):
        self.assertError(HEADER + "message M {\n a: u8[..];\n b: u8; }", "must be the last", 3)

    def test_open_struct_not_last(self):
        self.assertError(HEADER + "message M { t: T; b: u8; } struct T { r: u8[..]; }",
                         "runs to the end")


    # length fields
    def test_length_field_later(self):
        self.assertError(HEADER + "message M { d: u8[n]; n: u8; }", "declared earlier")

    def test_length_field_not_int(self):
        self.assertError(HEADER + "message M { n: cstr; d: u8[n]; }", "must be an unsigned")

    def test_length_field_array(self):
        self.assertError(HEADER + "message M { n: u8[2]; d: u8[n]; }", "must be an unsigned")

    def test_length_field_signed(self):
        self.assertError(HEADER + "message M { n: i8; d: u8[n]; }", "must be an unsigned")

    def test_length_field_sizes_two_arrays(self):
        self.assertError(HEADER + "message M { n: u8; a: u8[n]; b: u8[n]; }",
                         "already sizes 'a'")

    def test_sized_array_not_u8(self):
        self.assertError(HEADER + "message M { n: u8; d: u16[n]; }", "must be u8[n]")

    # switches
    def test_switch_tag_later(self):
        self.assertError(HEADER + """
            enum K : u8 { A = 1 }
            message M { body: switch (k) { A => X, }; k: K; }
            struct X { a: u8; }""", "earlier field")

    def test_switch_tag_not_enum(self):
        self.assertError(HEADER + """
            message M { k: u8; body: switch (k) { A => X, }; }
            struct X { a: u8; }""", "enum type")

    def test_arm_unknown_variant(self):
        self.assertError(HEADER + TAGGED % "A => X, D => Y,", "D is not a variant")

    def test_arm_duplicate_tag(self):
        self.assertError(HEADER + TAGGED % "A => X, A, B => Y,", "more than one arm")

    def test_arm_not_struct(self):
        self.assertError(HEADER + TAGGED % "A => u16,", "must be a struct")

    def test_wild_not_last(self):
        self.assertError(HEADER + TAGGED % "_ => u8[..], A => X,", "must be last")

    def test_wild_with_tags(self):
        self.assertError(HEADER + TAGGED % "A => X, _, B => u8[..],", "only tag")

    def test_wild_type(self):
        self.assertError(HEADER + TAGGED % "A => X, _ => Y,", "u8[..]")

    def test_open_switch_not_last(self):
        self.assertError(HEADER + """
            enum K : u8 { A = 1 }
            message M { k: K; body: switch (k) { _ => u8[..], }; after: u8; }""",
                         "runs to the end")

    def test_wild_member_collision(self):
        self.assertError(HEADER + """
            enum K : u8 { A = 1 }
            message M { k: K; body: switch (k) { A => Unknown, _ => u8[..], }; }
            struct Unknown { a: u8; }""", "body.unknown")

    # whole schema
    def test_direct_cycle(self):
        self.assertError(HEADER + "message M { s: S; } struct S { s: S; }", "contains itself")

    def test_indirect_cycle(self):
        self.assertError(HEADER + """
            message M { a: A; }
            struct A { b: B; }
            struct B { list: A[..]; }""", "A -> B -> A")

    def test_c_name_collision(self):
        self.assertError(HEADER + """
            message M { a: FooBar; b: Foo_bar; }
            struct FooBar { x: u8; }
            struct Foo_bar { y: u8; }""", "also used by FooBar")

    def test_reserved_c_name(self):
        self.assertError(HEADER + "message M { s: Bytes; } struct Bytes { x: u8; }",
                         "the generated code")

    def test_list_c_name_collision(self):
        self.assertError(HEADER + """
            message M { a: OptionList; b: Option[..]; }
            struct Option { x: u8; }
            struct OptionList { y: u8; }""", "also used by")

    def test_switch_c_name_collision(self):
        self.assertError(HEADER + """
            enum K : u8 { A = 1 }
            message M { k: K; body: switch (k) { A => MBody, }; }
            struct MBody { x: u8; }""", "also used by")

    def test_enum_constant_collision(self):
        self.assertError(HEADER + """
            enum A : u8 { B_C = 1 }
            enum A_B : u8 { C = 1 }
            message M { a: A; b: A_B; }""", "<PROTO>_A_B_C")

    # bitfields
    def test_bitfields_fill_bytes(self):
        self.assertError(HEADER + "message M { a: u3; b: u4; c: cstr; }", "whole bytes")

    def test_bitfields_at_end(self):
        self.assertError(HEADER + "message M { a: u8; b: u4; }", "whole bytes")

    def test_bit_group_too_wide(self):
        self.assertError(HEADER + "message M { a: u4; b: u32; c: u4; }", "more than 32 bits")

    def test_enum_on_bits_fits(self):
        self.assertError(HEADER + "enum E : u3 { A = 8 } message M { e: E; _: u5; }",
                         "does not fit in u3")

    def test_bad_int_width(self):
        self.assertError(HEADER + "message M { a: u33; }", "unknown type 'u33'")

    def test_array_of_bits(self):
        self.assertError(HEADER + "message M { a: u4[2]; }", "array elements must be")

    # constant and reserved fields
    def test_const_fits(self):
        self.assertError(HEADER + "message M { a: u8; r: u1 = 2; _: u7; }", "does not fit in u1")

    def test_const_not_int(self):
        self.assertError(HEADER + "message M { a: u8; c: cstr = 1; }", "only an unsigned integer field")

    def test_const_signed(self):
        self.assertError(HEADER + "message M { a: u8; c: i8 = 1; }",
                         "only an unsigned integer field")

    def test_reserved_signed(self):
        self.assertError(HEADER + "message M { a: u8; _: i8; }", "reserved field must be")

    def test_signed_enum_base(self):
        self.assertError(HEADER + "enum E : i8 { A = 1 } message M { e: E; }", "base type")

    def test_signed_array(self):
        self.assertError(HEADER + "message M { a: i8[4]; }", "array elements must be")

    def test_signed_builtin_name(self):
        self.assertError(HEADER + "message M { a: u8; } struct i16 { b: u8; }", "built-in")

    def test_reserved_not_int(self):
        self.assertError(HEADER + "message M { a: u8; _: cstr; }", "reserved field must be")

    def test_reserved_names_may_repeat(self):
        self.assertEqual(errors(HEADER + "message M { a: u8; _: u8; _: u16[2]; }"), [])

    def test_constant_length_field(self):
        self.assertError(HEADER + "message M { n: u8 = 4; d: u8[n]; }", "cannot be constant")

    def test_nothing_to_store(self):
        self.assertError(HEADER + "message M { v: u8 = 2; _: u8; }", "no fields to store")

    # declarations: const, max
    def test_const_decl_collision(self):
        self.assertError(HEADER + "const OK: u8 = 1; message M { a: u8; }", "<PROTO>_OK")

    def test_const_type(self):
        for t in ("u0", "u33", "i8", "Foo", "cstr"):
            self.assertError(HEADER + f"const C: {t} = 1; message M {{ a: u8; }}",
                             "type must be u1..u32")

    def test_const_value_fits(self):
        self.assertError(HEADER + "const V: u3 = 8; message M { a: u8; }", "does not fit in u3")
        self.assertEqual(errors(HEADER + "const V: u3 = 7; message M { a: u8; }"), [])

    def test_max_on_struct(self):
        self.assertError(HEADER + "message M { a: u8; } struct S max 4 { a: u8; }",
                         "only a message")

    def test_max_below_min(self):
        self.assertError(HEADER + "message M max 3 { a: u32; rest: u8[..]; }",
                         "less than its minimum size 4")

    def test_max_without_effect(self):
        self.assertError(HEADER + "message M max 8 { a: u32; }", "has no effect")

    def test_max_ok(self):
        self.assertEqual(errors(HEADER + "message M max 100 { a: u32; rest: u8[..]; }"), [])

    def test_reports_all_errors(self):
        self.assertEqual(len(errors(HEADER + "message M { a: Nope; b: Nada; }")), 2)


class Bounds(unittest.TestCase):
    def bounds(self, text):
        s = parse(HEADER + text)
        check(s)
        return size_bounds(s)

    def test_tftp(self):
        b = size_bounds(parse((ROOT / "examples/tftp/tftp.schema").read_text()))
        self.assertEqual(b["Packet"], (2, None))
        self.assertEqual(b["Ack"], (2, 2))

    def test_fixed(self):
        self.assertEqual(self.bounds("message M { a: u4; b: u12; c: u8[3]; d: u32; }")["M"],
                         (9, 9))

    def test_sized_and_pad(self):
        b = self.bounds("message M { c: u8; n: u8; d: u8[n] pad 4; }")
        self.assertEqual(b["M"], (2, 258))  # 2 + 255 rounded up to 256

    def test_sized_list(self):
        b = self.bounds("message M { n: u16; s: S[n]; } struct S { a: u32; }")
        self.assertEqual(b["M"], (2, 2 + 0xffff))

    def test_switch(self):
        b = self.bounds("""enum K : u8 { A = 1, B = 2 }
            message M { k: K; body: switch (k) { A => X, B => Y, }; }
            struct X { x: u8; } struct Y { y: u32; }""")
        self.assertEqual(b["M"], (2, 5))


class Syntax(unittest.TestCase):
    def assertSyntax(self, text: str, line: int, column: int, msg: str):
        with self.assertRaises(ParseError) as cm:
            parse(text)
        e = cm.exception
        self.assertEqual((e.line, e.column, e.msg), (line, column, msg))

    def test_unexpected_token(self):
        self.assertSyntax(HEADER + "message M { a: u8 }", 2, 19, "unexpected '}'")

    def test_unexpected_character(self):
        self.assertSyntax(HEADER + "message M { a: u8; $ }", 2, 20, "unexpected character '$'")

    def test_end_of_file(self):
        self.assertSyntax(HEADER + "message M { a: u8;", 2, 18, "unexpected end of file")

    def test_old_pad_statement(self):
        self.assertSyntax(HEADER + "message M { n: u8; d: u8[n];\n pad(4); }", 3, 5,
                          "unexpected '('")


class Snake(unittest.TestCase):
    def test_snake(self):
        for name, want in [("Oack", "oack"), ("OptionAck", "option_ack"),
                           ("HTTPServer", "http_server"), ("RRQ", "rrq"),
                           ("PcpOption", "pcp_option"), ("Ipv6Addr", "ipv6_addr"),
                           ("snake_case", "snake_case")]:
            self.assertEqual(snake(name), want)


if __name__ == "__main__":
    unittest.main()
