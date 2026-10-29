# pktc design

pktc is a packet compiler. It reads a small schema describing the wire format
of a simple network protocol and generates dependency-free C23 code that
encodes and decodes it, in a sans-io style: the generated code only turns
bytes into structs and structs into bytes, and never does I/O itself.

## Scope

Target protocols are small, fixed, RFC-defined protocols such as TFTP
(RFC 1350, options per RFC 2347), PCP (RFC 6887), NTP and STUN.

pktc is deliberately not general-purpose. Most of protobuf's weight comes
from schema evolution (field numbers, unknown fields, optional presence,
varints, reflection, a runtime). For these protocols the RFC is the schema
and it does not change, so pktc has none of that.

**Rule for adding features:** a construct goes into the language only when
a real protocol on the target list needs it. Anything unusual (DNS name
compression, STUN's XOR-mapped addresses and CRC fingerprint) goes through
an escape hatch, where generated code calls a hand-written function for that
field, instead of growing the language. The escape hatch is not designed yet.

## Schema language

The feature set, derived from TFTP and PCP:

| Construct | Syntax | Needed by |
|---|---|---|
| Big-endian u8/u16/u32 | `block: u16;` | both |
| Bitfields within a byte | not designed yet | PCP (R bit + 7-bit opcode) |
| Reserved/constant fields | `reserved: u8;` (constants not designed yet) | PCP |
| Fixed byte arrays | `nonce: u8[12];` | PCP (addresses, nonce) |
| Switch on a tag field | `body: switch (opcode) { RRQ, WRQ => Request, ... };` | both |
| NUL-terminated string | `filename: cstr;` | TFTP |
| Length from an earlier field | `len: u16 = sizeof(data); data: u8[len];` | PCP options |
| Rest of message | `data: u8[..];` | TFTP DATA |
| Repeat until end of message | `options: Option[..];` | both |
| Pad to N-byte boundary | `pad(4);` | PCP options |

`message` declares a top-level packet; `struct` declares a reusable group of
fields. An `enum Name : u16 { A = 1, ... }` names tag and code values.

Not supported: little-endian, varints, checksums, arbitrary expressions,
nested length scopes, optional fields, versioning.

See `examples/tftp/tftp.schema` and `tests/syntax.schema`.

## Generated C

`examples/tftp/tftp_gen.h` is the hand-written target for what the generator
should emit for TFTP.

- **One C struct per schema struct, and a tagged union per `switch`.** The C
  mirrors the schema directly. Schema arms that share a type share a union
  member (RRQ and WRQ both use `request`).
- **C names come from schema names by a fixed rule; the generator never
  invents them.** Types become `<proto>_<snake_case>` (`Oack` →
  `tftp_oack`), and a union member is the snake_case name of its arm's type
  (`body.oack`). Choose the schema name to get the C name you want.
- **Views, not copies.** Variable-length fields are pointer + length into the
  decode buffer. The decoded struct borrows the buffer, which must stay alive
  and unchanged while the struct is used. Decoding allocates nothing.
  - `cstr`: decode checks the NUL is inside the buffer, so `ptr` is also a
    valid C string; `len` excludes the terminator. Encode writes the NUL from
    `len`, so the caller's `ptr` needn't be NUL-terminated.
  - `u8[...]`: `{ const uint8_t *ptr; size_t len; }`.
  - Arrays of multi-byte elements (`u16[]`) cannot be typed pointers, because
    of byte order, alignment and strict aliasing. They become a view plus a
    generated `_get(i)` accessor that reads byte by byte with shifts, which
    compilers turn into a single load (plus a byte swap if needed).
  - Lists of structs (`Option[..]`) become a view plus an iterator.
- **Scalars are decoded into the struct** with byte-by-byte shifts. Never cast
  a packed struct over the buffer.
- **Enums are C23 fixed-type enums** (`enum tftp_opcode : uint16_t`). This
  gives a guaranteed size and `-Wswitch` warnings for missing cases. A
  fixed-type enum can hold any value of its base type, so the enum type does
  no validation by itself. The generated code targets C23 with no
  pre-C23 fallback.
- **Struct lists hold both a decode and an encode form.**
  `tftp_option_list` has `raw`/`raw_len` (decode: the validated wire bytes,
  walked by the iterator) and `items`/`count` (encode: a caller array).
  Encode uses `items` if it is non-NULL, otherwise it copies `raw`, so
  decode followed by encode reproduces the original bytes. The alternatives
  considered were an encode builder API and separate decode/encode structs.
- **API per message:** `_decode(out, buf, len)`, `_encode(in, buf, cap,
  &written)` and `_encoded_size(in)`. The statuses are `OK`, `INVALID`
  and `NOSPACE` (encode only). Encode returns `INVALID` for input it cannot
  encode (unknown tag, NUL in a `cstr`, hand-filled `raw` list bytes that
  decode would reject), so encode never emits bytes that decode rejects.
  `_encoded_size` assumes valid input and returns 0 for an unknown tag.
  `*written` is set only on `OK`. Datagram protocols have no `INCOMPLETE`;
  a stream protocol would add one that reports how many bytes are still
  needed.
- **Generated code layout:** two bounds-checked cursors (read and write)
  with primitive helpers (`rd_u16`, `rd_cstr`, `wr_u16`, ...), then one
  static `_decode`/`_encode`/`_size` triple per schema struct, each calling
  the helpers or other structs' triples field by field. Only the message
  functions and list iterators are public. The only library dependency is
  `<string.h>` (`memchr`, `memcpy`), which C23 freestanding provides.
- **Encoding a variable-length payload copies it** into the output buffer.
  Scatter-gather output (header bytes plus pointers for `writev`) and a
  reserve-then-fill API are possible later additions.

## Validation

- **The codec enforces structure only.** That means everything needed for
  views to be safe and framing to be correct: lengths and counts in bounds,
  the NUL present, known union tags, constants matching, and the message
  consuming the whole datagram (trailing bytes are `INVALID`; a protocol
  that tolerates them would end with a `u8[..]` field). On encode it means,
  for example, no embedded NUL in a `cstr`. A successful decode guarantees
  the struct is safe to read ("parse, don't validate").
- **Lenient by default, where the RFCs ask for it:**
  - Reserved fields are ignored on decode and written as zero on encode
    (PCP requires this).
  - Union tags are closed: an unknown tag is `INVALID`, unless the switch
    has a `_ => u8[..]` fallback arm, which generates `body.unknown` as a
    byte view. PCP needs the fallback to answer an unsupported opcode with
    `UNSUPP_OPCODE`.
  - Enums that do not select a union branch are open: unknown values are
    stored as-is.
- **Semantic checks are not the codec's job.** Examples are TFTP's mode
  strings, block size ranges, expected block numbers, and PCP's
  mandatory-to-process option codes (0–127). Many depend on session state.
  They belong in the protocol state machine, written by hand.
- No constraint language for now. If the same kind of check keeps recurring
  across protocols, consider a small annotation such as `in 8..65464` that
  generates a separate, optional `_check()` function.

## Compiler pipeline

```
schema text --parse--> AST --check--> IR --emit--> <proto>_gen.h + <proto>_gen.c
```

Written in Python, which is a build-time dependency only: the generated C
depends on nothing.

1. **Parse (done):** `schema.py`, a Lark grammar plus a transformer into
   dataclasses. `python3 schema.py FILE` prints the AST. Lark can later emit
   a standalone parser (`python -m lark.tools.standalone`) to drop the
   dependency.
2. **Check (next):**
   - every type name resolves;
   - a switch tag is an earlier field of enum type, and each arm's tags are
     variants of that enum with no duplicates;
   - a length field comes before the array it sizes, and a `= sizeof(x)`
     target exists and is that array;
   - `[..]` must be last, and a struct containing `[..]` must itself be last
     wherever it is used;
   - bitfields fill whole bytes.
3. **Lower to an IR:** a flat list of wire operations per struct (read u16 BE,
   read cstr, read u8 array sized by field `len`, pad to 4, dispatch on
   `opcode`), with fixed offsets computed where possible. Decode, encode and
   size are all generated from the same list, so they cannot disagree.
4. **Emit:** plain Python f-strings and a small helper that tracks
   indentation, not a template engine.

## Testing

- **Golden files:** commit the generated `tftp_gen.h` and `tftp_gen.c`; tests
  regenerate them and diff.
- **Round trip:** decode followed by encode gives back the same bytes.
- **Fuzzing (libFuzzer via clang):** decode never crashes or reads out of
  bounds, and anything that decodes re-encodes to identical bytes.

## Next steps

1. ~~Hand-write `examples/tftp/tftp_gen.c` to match `tftp_gen.h`, with a
   round-trip test and a fuzz harness.~~ Done: `tftp_gen.c` is the
   emitter's target output; `test_tftp.c` holds the round-trip and edge-case
   tests; `fuzz_tftp.c` is the libFuzzer harness (build command in its
   header comment).
2. Write the checker.
3. Write the IR and C emitter, and make it reproduce the hand-written TFTP
   code.
4. Add PCP: bitfields, fixed arrays, length fields, padding and the `_`
   fallback arm.
