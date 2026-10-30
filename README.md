# pktc

pktc is a packet compiler. You describe an existing wire format, such as the
header and attributes of a STUN message, in a small schema language. pktc
generates C that decodes and encodes it:

- **Both directions from one description:** decode, encode and size are
  generated from the same field list, so they cannot disagree.
- **No dependencies:** plain C23 needing only `<string.h>`. No runtime
  library and no allocation; it works in freestanding environments.
- **Bounds-checked:** decoding a hostile packet either succeeds or returns
  `INVALID`. Encode never writes bytes that decode would reject.
- **Sans-io:** bytes in, struct out; struct in, bytes out. Sockets, retries
  and timers stay in your code.
- **A fuzz harness for every schema:** `--fuzz` also writes a libFuzzer
  harness.

It targets small, fixed protocols defined by RFCs. The examples are TFTP,
PCP, NTP and STUN.

## Why another tool?

Most serialization tools design their own wire format; pktc describes one
that already exists. The tools that do describe existing formats mostly
parse only, or need a runtime:

| Tool | Describes existing formats | Encodes too | Plain C, no runtime |
|---|---|---|---|
| Protocol Buffers, nanopb, FlatBuffers, Cap'n Proto | no: they define their own | yes | nanopb: a small C runtime; others no |
| ASN.1 compilers (asn1c, ...) | only ASN.1-defined protocols | yes | needs its support code |
| [Kaitai Struct](https://kaitai.io) | yes | Java and Python only ([v0.11](https://kaitai.io/news/2025/09/07/kaitai-struct-v0.11-released.html)) | no C target (C++, with a runtime) |
| [EverParse 3D](https://project-everest.github.io/everparse/3d.html) | yes | no: it generates verified validators | yes |
| Construct, Scapy (Python); binrw, deku (Rust) | yes | yes | no: they are Python or Rust |
| Hand-written C | yes | yes | yes, but every bounds check is yours |

pktc covers the last column: the format from the RFC, in both directions,
as C you can drop into firmware or a daemon.

In exchange, it does deliberately little:
- **No schema evolution:** no field numbers, optional fields or versioning.
  The RFC is the schema, and it does not change.
- **No semantic checks:** the codec checks structure (lengths, tags,
  constants, framing). Protocol state, such as "this block number should be
  the next one", is your code's job.
- **Big-endian only, and no checksums:** values computed over bytes, like
  STUN's CRC fingerprint, are computed by the caller (see
  [`test_stun.c`](examples/stun/test_stun.c)).

## Example

Part of [`examples/stun/stun.pkt`](examples/stun/stun.pkt)
(RFC 8489):

```
message Message {
    zero: u2 = 0;                   // constant: decode rejects anything else
    type: Type;                     // an enum on 14 bits
    length: u16;                    // byte length of the attributes
    cookie: u32 = 0x2112A442;
    transaction_id: u8[12];
    attributes: Attribute[length];
}

struct Attribute {
    type: AttributeType;
    length: u16;
    value: u8[length] pad 4;        // zero-padded to a multiple of 4 bytes
}
```

`python3 pktc.py examples/stun/stun.pkt` writes `stun_gen.h` and
`stun_gen.c`. Using them:

```c
#include <stdio.h>
#include <string.h>
#include "stun_gen.h"

/* Print the attributes of a received STUN message. */
void show(const uint8_t *buf, size_t len) {
    struct stun_message msg;
    if (stun_message_decode(&msg, buf, len) != STUN_OK)
        return;  /* malformed: drop it */

    struct stun_attribute_iter it;
    struct stun_attribute attr;
    stun_attribute_iter_init(&it, &msg.attributes);
    while (stun_attribute_iter_next(&it, &attr))
        printf("attribute 0x%04x, %zu bytes\n", attr.type, attr.value.len);
}

/* Encode a Binding request with one SOFTWARE attribute. */
size_t binding_request(uint8_t *buf, size_t cap, const uint8_t txid[12]) {
    struct stun_attribute software = {
        .type = STUN_ATTRIBUTE_TYPE_SOFTWARE,
        .value = { (const uint8_t *)"example", 7 },  /* padded on the wire */
    };
    struct stun_message msg = {
        .type = STUN_TYPE_BINDING_REQUEST,
        .attributes = { .items = &software, .count = 1 },
    };
    memcpy(msg.transaction_id, txid, 12);
    size_t n = 0;
    return stun_message_encode(&msg, buf, cap, &n) == STUN_OK ? n : 0;
}
```

You never set `length`: encode measures the attributes and writes it, and
decode checks that it matches.

## Getting started

You need:
- Python 3.10 or later, with [Lark](https://github.com/lark-parser/lark)
  (`pip install lark`), to run the compiler from source or build it.
- A C23 compiler for the generated code. The generated code uses
  `constexpr` and enums with a fixed underlying type. It is tested with
  GCC 16 and Clang 23.

```sh
make                                   # builds pktc.pyz
./pktc.pyz my.pkt                      # writes <protocol>_gen.h and _gen.c
./pktc.pyz my.pkt -o out/ --fuzz       # also writes <protocol>_fuzz.c
```

`pktc.pyz` is a single file with Lark inside: copy it anywhere that has
Python 3.10 or later. Errors point at the schema, for example
`my.pkt:12: Opt.data: length field 'n' must be declared earlier in the
same struct`.

## The schema language

| Construct | Syntax |
|---|---|
| Protocol name (the C prefix) | `protocol pcp;` |
| Big-endian integers, 1 to 32 bits; bitfields pack MSB-first | `id: u24;`, `r: u1; opcode: Opcode;` |
| Signed integers | `poll: i8;` |
| Enums on any width | `enum Opcode : u7 { MAP = 1, PEER = 2 }` |
| Constant field: checked on decode, written on encode | `r: u1 = 0;` |
| Reserved: ignored on decode, zeros on encode | `_: u24;`, `_: u8[12];` |
| Fixed arrays | `nonce: u8[12];`, `words: u16[3];` |
| NUL-terminated string | `filename: cstr;` |
| Bytes with a length from an earlier field | `len: u16; data: u8[len];` |
| ... padded to a multiple of N bytes | `data: u8[len] pad 4;` |
| Rest of the message | `data: u8[..];` |
| Structs repeated to the end, or filling a length | `options: Option[..];`, `attrs: Attribute[len];` |
| Tagged union on an earlier enum field | `body: switch (opcode) { MAP => Map, _ => u8[..], };` |
| Named constant | `const SERVER_PORT: u16 = 5351;` |
| Maximum message size, enforced both ways | `message Request max 1100 { ... }` |

Schemas use the `.pkt` extension by convention. pktc accepts any file
name, and names its output after the `protocol` declaration.

A `message` is something you decode from a buffer, and gets the public
`_decode`/`_encode`/`_encoded_size` functions. A `struct` is a reusable
group of fields. Length fields are never set by hand: `len` in
`len: u16; data: u8[len];` has no C member at all. The four schemas in
[`examples/`](examples) show every construct in use, and
[`DESIGN.md`](DESIGN.md) explains each rule and why it is there.

## The generated C

For each `message M` in protocol `p`:

```c
p_status p_m_decode(struct p_m *out, const uint8_t *buf, size_t len);
p_status p_m_encode(const struct p_m *in, uint8_t *buf, size_t cap, size_t *written);
size_t   p_m_encoded_size(const struct p_m *in);
constexpr size_t P_M_MIN_SIZE = ...;   /* and P_M_MAX_SIZE when it is bounded */
```

- **Statuses:**
  - `P_OK`;
  - `P_INVALID`: malformed input on decode, or a value encode cannot
    represent on encode (too wide for its field, an unknown tag);
  - `P_NOSPACE`: the output buffer is too small.
- **Variable-length fields are views:** `{ptr, len}` pointing into the buffer
  you decoded. Keep the buffer alive and unchanged while you use them.
  Decoding allocates nothing.
- **Fixed fields are copied in:** integers, fixed arrays, enums (C23 enums
  with the schema's width).
- **Lists decode lazily:** a decoded list is validated, then walked with an
  iterator. To encode one, point `.items`/`.count` at your array.
- **Unions are tagged by a field:** `msg.body.map` is valid when
  `msg.opcode == PCP_OPCODE_MAP`.

## Testing and fuzzing

Each example has hand-written tests built from its RFC. The STUN tests use
the RFC 5769 test vectors. The generated fuzz harness uses each input twice:
- **as a packet to decode:** it must re-encode to the same bytes (reserved
  fields and padding come back as zeros);
- **as a recipe:** it picks tags, lengths and values to build a valid
  message, which must encode, decode and re-encode identically.

The second half reaches deep structure that random bytes rarely do.

```sh
make test                         # compiler tests (runs each fuzzer briefly)
make check                        # each example's C tests, with sanitizers
make fuzz-stun FUZZ_SECONDS=600   # a long fuzz run (needs clang)
make examples                     # regenerate examples/ after changing pktc
```

## License

MIT; see [LICENSE](LICENSE). Code generated by pktc is yours, under any
license you like.
