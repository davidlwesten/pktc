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
from emit import emit, emit_fuzz  # noqa: E402
from ir import lower  # noqa: E402
from pktc import compile_schema  # noqa: E402
from schema import parse  # noqa: E402

CFLAGS = ["-std=c23", "-Wall", "-Wextra", "-pedantic", "-Werror"]
SANITIZE = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
HAVE_GCC = shutil.which("gcc") is not None
HAVE_CLANG = shutil.which("clang") is not None
HAVE_MAKE = shutil.which("make") is not None
FUZZ_SECONDS = 3
EXAMPLES = ["tftp", "pcp", "ntp", "stun"]

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


# Covers what PCP does not: a 16-bit group holding a constant, a 3-bit enum,
# a 6-bit length and 3-bit flags (all at different shifts), a u24 member,
# signed fields (a whole i8, and an i12 with bits below it), a u16 array, a
# reserved array, padding mid-message, and a computed MAX_SIZE.
BITS = """
protocol bits;

enum Mode : u3 { OFF = 0, ON = 5 }

message Rec {
    ver: u4 = 9;
    mode: Mode;
    n: u6;
    flags: u3;
    id: u24;
    temp: i8;
    off: i12;
    k: u4;
    words: u16[3];
    _: u8[2];
    data: u8[n] pad 8;
    last: u32;
}
"""

BITS_TEST = r"""
#include <stdio.h>
#include <string.h>
#include "bits_gen.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static_assert(BITS_REC_MIN_SIZE == 20);  /* 16 + no data + 4 */
static_assert(BITS_REC_MAX_SIZE == 84);  /* 16 + 63 padded to 64 + 4 */

int main(void) {
    /* 9 << 12 | ON << 9 | 5 << 3 | 6 == 0x9a2e */
    static const uint8_t rec[] = {
        0x9a, 0x2e, 0x12, 0x34, 0x56,
        0xfd, 0xff, 0xe5,  /* temp -3, off -2, k 5 */
        0x01, 0x02, 0x03, 0x04, 0xff, 0xfe, 0, 0,
        'a', 'b', 'c', 'd', 'e', 0, 0, 0, 0xde, 0xad, 0xbe, 0xef,
    };
    struct bits_rec r;
    CHECK(bits_rec_decode(&r, rec, sizeof rec) == BITS_OK);
    CHECK(r.mode == BITS_MODE_ON && r.flags == 6 && r.id == 0x123456);
    CHECK(r.temp == -3 && r.off == -2 && r.k == 5);
    CHECK(r.words[0] == 0x0102 && r.words[2] == 0xfffe);
    CHECK(r.data.len == 5 && memcmp(r.data.ptr, "abcde", 5) == 0);
    CHECK(r.last == 0xdeadbeef);

    uint8_t out[128];
    size_t n = 0;
    CHECK(bits_rec_encoded_size(&r) == sizeof rec);
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_OK);
    CHECK(n == sizeof rec && memcmp(out, rec, n) == 0);
    for (size_t cap = 0; cap < sizeof rec; cap++)
        CHECK(bits_rec_encode(&r, out, cap, &n) == BITS_NOSPACE);

    uint8_t bad[sizeof rec];
    memcpy(bad, rec, sizeof bad);
    bad[0] = 0x8a;  /* ver 8, not 9 */
    CHECK(bits_rec_decode(&r, bad, sizeof bad) == BITS_INVALID);
    CHECK(bits_rec_decode(&r, rec, sizeof rec - 1) == BITS_INVALID);

    CHECK(bits_rec_decode(&r, rec, sizeof rec) == BITS_OK);
    r.flags = 8;  /* too wide for u3 */
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_INVALID);
    r.flags = 0;
    r.id = 0x1000000;  /* too wide for u24 */
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_INVALID);
    r.id = 0;
    r.off = 2048;  /* too wide for i12 */
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_INVALID);
    r.off = -2049;
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_INVALID);
    r.off = -2048;
    r.temp = -128;
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_OK);
    CHECK(out[5] == 0x80 && out[6] == 0x80 && out[7] == 0x05);
    struct bits_rec back;
    CHECK(bits_rec_decode(&back, out, n) == BITS_OK);
    CHECK(back.temp == -128 && back.off == -2048 && back.k == 5);
    static const uint8_t big[64] = { 0 };
    r.data = (struct bits_bytes){ big, sizeof big };  /* too long for the u6 length */
    CHECK(bits_rec_encode(&r, out, sizeof out, &n) == BITS_INVALID);

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


def fuzz_source(text: str, source: str = "test.schema") -> str:
    s = parse(text)
    check(s)
    return emit_fuzz(lower(s, source))


class Golden(unittest.TestCase):
    """The committed example code must be what the generator produces now."""
    def check_example(self, proto: str):
        d = ROOT / "examples" / proto
        name, files = compile_schema(d / f"{proto}.schema", fuzz=True)
        self.assertEqual(name, proto)
        stale = f"is stale: run python3 pktc.py examples/{proto}/{proto}.schema --fuzz"
        for fname, text in files.items():
            self.assertEqual(text, (d / fname).read_text(), f"{fname} {stale}")

    def test_tftp(self):
        self.check_example("tftp")

    def test_pcp(self):
        self.check_example("pcp")

    def test_ntp(self):
        self.check_example("ntp")

    def test_stun(self):
        self.check_example("stun")


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

    def test_no_max_size_when_unbounded(self):
        h, _ = generate(DEMO)
        self.assertIn("DEMO_HELLO_MIN_SIZE", h)
        self.assertNotIn("DEMO_HELLO_MAX_SIZE", h)

    def test_lines_fit(self):
        examples = [(ROOT / "examples" / p / f"{p}.schema").read_text() for p in EXAMPLES]
        for text in (DEMO, BITS, *examples):
            h, c = generate(text)
            f = fuzz_source(text)
            long = [l for l in (h + c + f).splitlines() if len(l) > 79]
            self.assertEqual(long, [])

    def test_strict_round_trip_only_without_reserved(self):
        self.assertIn("give back the input exactly", fuzz_source(DEMO))
        self.assertIn("has reserved fields or padding", fuzz_source(BITS))

    def test_padding_in_lists_is_exact(self):
        # Decoded lists are re-encoded from their raw bytes, padding and all.
        self.assertIn("give back the input exactly", fuzz_source(
            "protocol p; message M { n: u8; s: S[n]; }"
            " struct S { _: u8; len: u8; v: u8[len] pad 4; }"))


@unittest.skipUnless(HAVE_MAKE, "make not found")
class Package(unittest.TestCase):
    """make builds pktc.pyz, which carries Lark inside and needs nothing
    outside the standard library (python3 -S skips site-packages)."""

    def test_pyz(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            pyz = d / "pktc.pyz"
            subprocess.run(["make", "-s", f"BUILD={d / 'build'}", f"PYZ={pyz}", str(pyz)],
                           cwd=ROOT, check=True, capture_output=True)
            run = subprocess.run([sys.executable, "-S", str(pyz),
                                  str(ROOT / "examples/stun/stun.schema"), "-o", tmp, "--fuzz"],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            for name in ("stun_gen.h", "stun_gen.c", "stun_fuzz.c"):
                self.assertEqual((d / name).read_text(),
                                 (ROOT / "examples/stun" / name).read_text(), name)

            bad = d / "bad.schema"
            bad.write_text("protocol p;\nmessage M { a: u8 }\n")
            run = subprocess.run([sys.executable, "-S", str(pyz), str(bad)],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertEqual(run.stderr, f"{bad}:2:19: unexpected '}}'\n")


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

    def test_bits(self):
        h, c = generate(BITS, "bits.schema")
        out = self.build_and_run({"bits_gen.h": h, "bits_gen.c": c, "t.c": BITS_TEST},
                                 ["t.c", "bits_gen.c"])
        self.assertEqual(out.strip(), "ok")

    def run_example(self, proto: str):
        d = ROOT / "examples" / proto
        names = (f"{proto}_gen.h", f"{proto}_gen.c", f"test_{proto}.c")
        files = {n: (d / n).read_text() for n in names}
        out = self.build_and_run(files, [f"test_{proto}.c", f"{proto}_gen.c"])
        self.assertEqual(out.strip(), "ok")

    def test_tftp(self):
        self.run_example("tftp")

    def test_pcp(self):
        self.run_example("pcp")

    def test_ntp(self):
        self.run_example("ntp")

    def test_stun(self):
        self.run_example("stun")


@unittest.skipUnless(HAVE_CLANG, "clang not found")
class Fuzz(unittest.TestCase):
    """Generate each protocol's libFuzzer harness and run it briefly. The
    encode-side recipes reach deep structure within seconds."""

    def fuzz(self, files: dict[str, str], harness: str, gen: str):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            for name, text in files.items():
                (d / name).write_text(text)
            build = subprocess.run(
                ["clang", *CFLAGS, "-g", "-fsanitize=fuzzer,address,undefined",
                 harness, gen, "-o", "fuzz"], cwd=d, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run(
                ["./fuzz", f"-max_total_time={FUZZ_SECONDS}", "-max_len=1200"],
                cwd=d, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr[-3000:])

    def fuzz_text(self, name: str, text: str):
        s = parse(text)
        check(s)
        proto = lower(s, f"{name}.schema")
        h, c = emit(proto)
        self.fuzz({f"{name}_gen.h": h, f"{name}_gen.c": c, f"{name}_fuzz.c": emit_fuzz(proto)},
                  f"{name}_fuzz.c", f"{name}_gen.c")

    def test_tftp(self):
        self.fuzz_text("tftp", (ROOT / "examples/tftp/tftp.schema").read_text())

    def test_pcp(self):
        self.fuzz_text("pcp", (ROOT / "examples/pcp/pcp.schema").read_text())

    def test_ntp(self):
        self.fuzz_text("ntp", (ROOT / "examples/ntp/ntp.schema").read_text())

    def test_stun(self):
        self.fuzz_text("stun", (ROOT / "examples/stun/stun.schema").read_text())

    def test_demo(self):
        self.fuzz_text("demo", DEMO)

    def test_bits(self):
        self.fuzz_text("bits", BITS)


if __name__ == "__main__":
    unittest.main()
