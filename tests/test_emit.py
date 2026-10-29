"""Generator tests. Run with: python3 -m unittest discover tests"""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from check import check  # noqa: E402
from emit import emit  # noqa: E402
from ir import lower  # noqa: E402
from pktc import compile_schema  # noqa: E402
from schema import parse  # noqa: E402

CFLAGS = ["-std=c23", "-Wall", "-Wextra", "-pedantic", "-Werror"]
SANITIZE = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
HAVE_GCC = shutil.which("gcc") is not None

# Covers what TFTP does not: u8/u32 fields, a u32 enum, an enum field that is
# not a switch tag, a nested struct, a switch in a non-message struct that
# covers only some variants, two arms of the same type, two messages, and an
# unused struct (which must not be emitted).
DEMO = """
protocol demo;

enum Kind : u8 { PING = 1, PONG = 2, INFO = 3, NOTE = 4 }
enum Color : u32 { RED = 0x10000, BLUE = 2 }

message Frame {
    version: u8;
    kind: Kind;
    stamp: u32;
    color: Color;
    hdr: Header;
    body: switch (kind) {
        PING, PONG => Echo,
        INFO       => Info,
        NOTE       => Echo,
    };
}

message Hello { name: cstr; items: Item[..]; }

struct Header { id: u16; flags: u8; }
struct Echo   { seq: u32; payload: u8[..]; }
struct Info   { k: Kind; inner: Inner; }
struct Inner  { tag: Kind; v: switch (tag) { PING => Header, }; }
struct Item   { key: u8; text: cstr; }
struct Unused { x: u8; }
"""

DEMO_TEST = r"""
#include <stdio.h>
#include <string.h>
#include "demo_gen.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void round_trip_frame(const uint8_t *buf, size_t len, struct demo_frame *f) {
    CHECK(demo_frame_decode(f, buf, len) == DEMO_OK);
    CHECK(demo_frame_encoded_size(f) == len);
    uint8_t out[64];
    size_t n = 0;
    CHECK(demo_frame_encode(f, out, sizeof out, &n) == DEMO_OK);
    CHECK(n == len && memcmp(out, buf, len) == 0);
    CHECK(demo_frame_encode(f, out, len - 1, &n) == DEMO_NOSPACE);
}

int main(void) {
    struct demo_frame f;

    /* NOTE: a second arm sharing the Echo member. */
    static const uint8_t note[] = {
        7, 4, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x00, 0x00,  /* version kind stamp color */
        0xab, 0xcd, 0x05,                                      /* hdr */
        0x00, 0x00, 0x00, 0x09, 'h', 'i',                      /* Echo */
    };
    round_trip_frame(note, sizeof note, &f);
    CHECK(f.version == 7 && f.kind == DEMO_KIND_NOTE);
    CHECK(f.stamp == 0x01020304 && f.color == DEMO_COLOR_RED);
    CHECK(f.hdr.id == 0xabcd && f.hdr.flags == 5);
    CHECK(f.body.echo.seq == 9 && f.body.echo.payload.len == 2);

    /* INFO: nested struct with its own switch. */
    static const uint8_t info[] = {
        1, 3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0,  /* header fields, color BLUE */
        4,                                      /* Info.k = NOTE (open enum) */
        1, 0x12, 0x34, 0x56,                    /* Inner: tag PING -> Header */
    };
    round_trip_frame(info, sizeof info, &f);
    CHECK(f.color == DEMO_COLOR_BLUE && f.body.info.k == DEMO_KIND_NOTE);
    CHECK(f.body.info.inner.tag == DEMO_KIND_PING);
    CHECK(f.body.info.inner.v.header.id == 0x1234);

    /* Inner's switch only covers PING. */
    static const uint8_t bad_inner[] = { 1, 3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 4, 2, 0, 0, 0 };
    CHECK(demo_frame_decode(&f, bad_inner, sizeof bad_inner) == DEMO_INVALID);

    /* Second message, with a list. */
    static const uint8_t hello[] = { 'x', 0, 1, 'a', 0, 2, 'b', 'c', 0 };
    struct demo_hello h;
    CHECK(demo_hello_decode(&h, hello, sizeof hello) == DEMO_OK);
    struct demo_item_iter it;
    struct demo_item item;
    demo_item_iter_init(&it, &h.items);
    CHECK(demo_item_iter_next(&it, &item) && item.key == 1 && strcmp(item.text.ptr, "a") == 0);
    CHECK(demo_item_iter_next(&it, &item) && item.key == 2 && strcmp(item.text.ptr, "bc") == 0);
    CHECK(!demo_item_iter_next(&it, &item));

    static const struct demo_item items[] = { { 1, DEMO_CSTR("a") }, { 2, DEMO_CSTR("bc") } };
    struct demo_hello h2 = { .name = DEMO_CSTR("x"), .items = { .items = items, .count = 2 } };
    uint8_t out[32];
    size_t n = 0;
    CHECK(demo_hello_encode(&h2, out, sizeof out, &n) == DEMO_OK);
    CHECK(n == sizeof hello && memcmp(out, hello, n) == 0);

    if (fails)
        return 1;
    puts("ok");
    return 0;
}
"""


def generate(text: str, source: str = "test.schema") -> tuple[str, str]:
    s = parse(text)
    check(s)
    return emit(lower(s, source))


class Golden(unittest.TestCase):
    def test_tftp_matches_committed(self):
        d = ROOT / "examples/tftp"
        name, h, c = compile_schema(d / "tftp.schema")
        self.assertEqual(name, "tftp")
        self.assertEqual(h, (d / "tftp_gen.h").read_text(),
                         "tftp_gen.h is stale: run python3 pktc.py examples/tftp/tftp.schema")
        self.assertEqual(c, (d / "tftp_gen.c").read_text(),
                         "tftp_gen.c is stale: run python3 pktc.py examples/tftp/tftp.schema")


class Output(unittest.TestCase):
    def test_unused_struct_not_emitted(self):
        h, c = generate(DEMO)
        self.assertNotIn("unused", h + c)

    def test_shared_arm_member(self):
        h, _ = generate(DEMO)
        self.assertIn("struct demo_echo echo;  /* PING, PONG, NOTE */", h)

    def test_only_used_primitives(self):
        _, c = generate("protocol p; message M { a: u16; }")
        self.assertIn("p_rd_u16", c)
        for absent in ("rd_u8", "rd_u32", "rd_cstr", "rd_rest", "wr_raw", "cstr_size"):
            self.assertNotIn(absent, c)

    def test_not_implemented(self):
        for body in ("a: u8[4];", "n: u8; a: u8[n];", "a: u8; pad(4);"):
            with self.subTest(body=body), self.assertRaisesRegex(NotImplementedError, "not implemented"):
                generate(f"protocol p; message M {{ {body} }}")
        with self.assertRaisesRegex(NotImplementedError, "'_' arm"):
            generate("""protocol p; enum K : u8 { A = 1 }
                        message M { k: K; b: switch (k) { A => S, _ => u8[..], }; }
                        struct S { x: u8; }""")


@unittest.skipUnless(HAVE_GCC, "gcc not found")
class Compiles(unittest.TestCase):
    def build_and_run(self, files: dict[str, str], sources: list[str]) -> str:
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            for name, text in files.items():
                (d / name).write_text(text)
            exe = d / "a.out"
            subprocess.run(["gcc", *CFLAGS, *SANITIZE, *sources, "-o", str(exe)],
                           cwd=d, check=True, capture_output=True, text=True)
            return subprocess.run([str(exe)], cwd=d, capture_output=True, text=True).stdout

    def test_demo(self):
        h, c = generate(DEMO, "demo.schema")
        out = self.build_and_run({"demo_gen.h": h, "demo_gen.c": c, "t.c": DEMO_TEST},
                                 ["t.c", "demo_gen.c"])
        self.assertEqual(out.strip(), "ok")

    def test_tftp(self):
        d = ROOT / "examples/tftp"
        files = {n: (d / n).read_text() for n in ("tftp_gen.h", "tftp_gen.c", "test_tftp.c")}
        out = self.build_and_run(files, ["test_tftp.c", "tftp_gen.c"])
        self.assertEqual(out.strip(), "ok")


if __name__ == "__main__":
    unittest.main()
