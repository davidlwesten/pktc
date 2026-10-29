"""Checker tests. Run with: python3 -m unittest discover tests"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from check import SchemaError, check, snake  # noqa: E402
from schema import parse  # noqa: E402

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
            struct Opt { code: u8; reserved: u8; len: u16 = sizeof(data); data: u8[len]; pad(4); }
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
        self.assertError(HEADER + "message M { a: u8; pad(0); }", "pad(0)")

    # arrays
    def test_zero_length_array(self):
        self.assertError(HEADER + "message M { a: u8[0]; }", "at least 1")

    def test_array_of_cstr(self):
        self.assertError(HEADER + "message M { a: cstr[..]; }", "arrays of cstr")

    def test_fixed_array_of_structs(self):
        self.assertError(HEADER + "message M { a: S[3]; } struct S { b: u8; }",
                         "must be 'S[..]'")

    def test_open_list_element(self):
        self.assertError(HEADER + "message M { a: S[..]; } struct S { b: u8[..]; }",
                         "cannot be a list element")

    def test_rest_not_last(self):
        self.assertError(HEADER + "message M {\n a: u8[..];\n b: u8; }", "must be the last", 3)

    def test_open_struct_not_last(self):
        self.assertError(HEADER + "message M { t: T; b: u8; } struct T { r: u8[..]; }",
                         "runs to the end")

    def test_pad_after_rest(self):
        self.assertError(HEADER + "message M { a: u8[..]; pad(4); }", "must be the last")

    # length fields
    def test_length_field_later(self):
        self.assertError(HEADER + "message M { d: u8[n]; n: u8 = sizeof(d); }",
                         "declared earlier")

    def test_length_field_missing_sizeof(self):
        self.assertError(HEADER + "message M { n: u8; d: u8[n]; }", "= sizeof(d)")

    def test_length_field_not_int(self):
        self.assertError(HEADER + "message M { n: cstr; d: u8[n]; }", "must be u8, u16 or u32")

    def test_sizeof_unknown_target(self):
        self.assertError(HEADER + "message M { n: u8 = sizeof(x); }", "no field 'x'")

    def test_sizeof_target_not_sized_by_it(self):
        self.assertError(HEADER + "message M { n: u8 = sizeof(d); d: u8[4]; }", "d: u8[n]")

    def test_sizeof_on_non_int(self):
        self.assertError(HEADER + "message M { n: u8[2] = sizeof(d); d: u8[n]; }",
                         "sizeof field must be")

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

    def test_enum_constant_collision(self):
        self.assertError(HEADER + """
            enum A : u8 { B_C = 1 }
            enum A_B : u8 { C = 1 }
            message M { a: A; b: A_B; }""", "<PROTO>_A_B_C")

    def test_reports_all_errors(self):
        self.assertEqual(len(errors(HEADER + "message M { a: Nope; b: Nada; }")), 2)


class Snake(unittest.TestCase):
    def test_snake(self):
        for name, want in [("Oack", "oack"), ("OptionAck", "option_ack"),
                           ("HTTPServer", "http_server"), ("RRQ", "rrq"),
                           ("PcpOption", "pcp_option"), ("Ipv6Addr", "ipv6_addr"),
                           ("snake_case", "snake_case")]:
            self.assertEqual(snake(name), want)


if __name__ == "__main__":
    unittest.main()
