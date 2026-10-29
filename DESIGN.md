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
| Big-endian integers u1..u32 | `block: u16;`, `id: u24;` | both |
| Bitfields | `r: u1; opcode: Opcode;` (enum on `u7`) | PCP (R bit + 7-bit opcode) |
| Constant fields | `r: u1 = 0;` | PCP (R bit) |
| Reserved fields | `_: u24;`, `_: u8[12];` | PCP |
| Named constants | `const SERVER_PORT = 5351;` | PCP (version, ports) |
| Declared maximum size | `message Request max 1100 { ... }` | PCP (1100 bytes) |
| Fixed integer arrays | `nonce: u8[12];`, `words: u16[3];` | PCP (addresses, nonce) |
| Switch on a tag field | `body: switch (opcode) { RRQ, WRQ => Request, ... };` | both |
| NUL-terminated string | `filename: cstr;` | TFTP |
| Length from an earlier field | `len: u16; data: u8[len];` | PCP options |
| Rest of message | `data: u8[..];` | TFTP DATA |
| Repeat until end of message | `options: Option[..];` | both |
| Pad to N-byte boundary | `pad(4);` | PCP options |

`message` declares a top-level packet; `struct` declares a reusable group of
fields. An `enum Name : u16 { A = 1, ... }` names tag and code values; its
base can be any width from `u1` to `u32`.

**Integers and bitfields.** `uN` is an N-bit big-endian unsigned integer,
for N from 1 to 32. Consecutive integer and enum fields are packed MSB-first
into a group, which is read as one big-endian integer and split into its
fields. A group ends on the first byte boundary and may span at most 32
bits. A plain `u16` is simply a group of one field, and `u24` needs no
special case. In C each field gets the smallest `uintN_t` that holds it,
or its enum type. Encode returns `INVALID` for a value too wide for its
field rather than truncate it.

**Constant and reserved fields have no C member.** `r: u1 = 0;` must
equal 0: decode returns `INVALID` otherwise, and encode writes 0. The name
is documentation only. `_: u24;` (or `_: u8[12];`) is reserved: ignored on
decode, written as zeros on encode. Only check a value with a constant
when a mismatch should drop the message. PCP's R bit is a constant, but
its version is a plain field, because a server must answer an unknown
version with `UNSUPP_VERSION` rather than drop it. The caller compares it
against `const VERSION = 2;`.

**Fixed arrays are copied into the C struct** (`uint8_t nonce[12]`,
`uint16_t words[3]`), so callers fill them in directly. Elements are
`u8`, `u16` or `u32`, converted to host order.

**`pad(n)`** skips to the next multiple of n bytes, counted from the start
of the enclosing struct (for PCP, from the start of each option). Padding
is ignored on decode and written as zeros.

**Length fields are derived, not set.** In `len: u16; data: u8[len];`,
naming `len` as the array's length is the whole declaration: decode reads
`len` and takes that many bytes as `data`; encode writes `data.len` into
`len`, so the two can never disagree. The caller never sets `len`. A
length is always a plain byte count, which is why only `u8[len]` is
allowed. Lengths in other units (4-byte words, counts including a header)
would need expressions and go through the escape hatch. An earlier draft
spelled this `len: u16 = sizeof(data)`; it was dropped because it repeated
what `u8[len]` already says, and C's `sizeof` suggested the wrong meaning.

**Message sizes.** Every message gets a generated
`<PROTO>_<MSG>_MIN_SIZE`, which is always computable. It also gets
`<PROTO>_<MSG>_MAX_SIZE` when a maximum is known, for sizing receive
buffers. A maximum is known in two ways:

| Message | `max` | `_MAX_SIZE` | Enforced |
|---|---|---|---|
| bounded by its fields (NTP: 48 bytes) | no | computed | not needed |
| unbounded, RFC states a limit (PCP: 1100) | yes | declared value | decode `INVALID`, encode refuses |
| unbounded, no fixed limit (TFTP) | no | none | no |

`max` is optional. The checker rejects a `max` below the computed minimum
(no valid message could fit) and a `max` at or above a computed maximum
(it could never apply). TFTP declares no `max`: its limit depends on the
negotiated `blksize`, which is session state, not wire format. Callers
without a `_MAX_SIZE` use the transport's limit (65,507 bytes for UDP).

**Named constants.** `const SERVER_PORT = 5351;` generates
`#define <PROTO>_SERVER_PORT 5351`, for protocol numbers callers need,
such as ports and versions.

**Deferred until a target protocol needs them:**
- *Count- and byte-length-prefixed arrays.* `[n]` is ambiguous for
  anything wider than a byte. Protocols use both meanings: DNS counts
  questions with a header field (`qs: Question[qdcount]`), while TLV
  containers (STUN attributes, DHCP options, NTP extensions) give a byte
  length (perhaps `opts: Option[bytes len]`). Counts are simple. A byte
  length around structs is a nested length scope: decode must fence off a
  sub-buffer and end exactly on an element boundary, and encode must
  measure before writing. Design these with STUN, DNS or DHCP.
- *Length-prefixed strings.* On the wire these are `u8[len]`. A string
  type would only change the C type to `const char *`, and unlike `cstr`
  it still would not be NUL-terminated.
- *Inline prefix syntax* such as `data: u8[u16]` for a length placed
  immediately before. The two-field form must stay, because many protocols
  put the length elsewhere, so add this only if schemas get noisy without it.

Not supported: little-endian, varints, checksums, arbitrary expressions,
nested length scopes, optional fields, versioning, empty structs.

See `examples/tftp/tftp.schema`, `examples/pcp/pcp.schema` and
`tests/syntax.schema`.

## Generated C

`examples/tftp/` and `examples/pcp/` hold generated code (`<proto>_gen.h`,
`<proto>_gen.c`) next to its schema, tests and fuzz harness. The TFTP code
started as a hand-written target for the emitter.

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
  - `u8[..]` and `u8[len]`: `{ const uint8_t *ptr; size_t len; }`.
  - Lists of structs (`Option[..]`) become a view plus an iterator.
  - Fixed arrays are the exception: they are copied (see above). A view
    into a buffer of `u16` elements could not be a typed pointer, because
    of byte order, alignment and strict aliasing; if variable-length
    arrays of wider elements are ever added, they need a `_get(i)`
    accessor instead.
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
  encode (unknown tag without a `_` arm, a value too wide for its field,
  NUL in a `cstr`, a message over its declared `max`, hand-filled `raw`
  list bytes that decode would reject), so encode never emits bytes that
  decode rejects. `_encoded_size` is exact when encode succeeds; for input
  encode rejects its result is meaningless. `*written` is set only on `OK`.
  Datagram protocols have no `INCOMPLETE`; a stream protocol would add one
  that reports how many bytes are still needed. The header also defines
  `<PROTO>_<MSG>_MIN_SIZE` and, where known, `_MAX_SIZE`.
- **Generated code layout:** two bounds-checked cursors (read and write)
  with primitive helpers (`rd_u16`, `rd_cstr`, `wr_u16`, ...), then one
  static `_read`/`_write`/`_size` triple per schema struct, each calling
  the helpers or other structs' triples field by field. A switch gets its
  own triple, `<struct>_<field>_read` etc., holding the C `switch`. A
  message's public `_decode`/`_encode`/`_encoded_size` wrap its triple.
  Reads and writes are one statement per field, `<P>_CHECK(...)` and
  `<P>_TRY(...)`, so a bit group can take several statements. Only the
  message functions and list iterators are public. Only the primitives
  the generated code calls are emitted (found by scanning it), and only
  structs reachable from a message, so `-Wunused-function` stays quiet.
  The only library dependency is `<string.h>` (`memchr`, `memcpy`,
  `memset`), which C23 freestanding provides.
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
  - Reserved fields (`_`) and padding are ignored on decode and written as
    zero on encode (PCP requires this). So for a schema with either,
    decode then encode reproduces the input except in those bytes.
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
2. **Check (done):** `check.py`, `python3 check.py FILE`. It collects every
   error with its line number instead of stopping at the first. After it
   passes, later stages may assume a well-formed schema. Tests:
   `python3 -m unittest discover tests`.
   - Declarations: a `protocol` and at least one `message`; names unique
     and not built-in; `endian little` is rejected (not supported).
   - Enums: base is u1..u32; variant names and values unique; values fit
     the base.
   - Bit groups: consecutive integer fields end on a byte boundary within
     32 bits.
   - Constant fields are integers and the value fits; reserved fields are
     integers or fixed integer arrays; a struct must keep at least one C
     member.
   - `max` only on messages, not below the computed minimum and not at or
     above a computed maximum. `size_bounds()` computes both, and the IR
     reuses it for the size constants.
   - Types: every name resolves; a `message` is never used as a type;
     no struct contains itself, directly or indirectly; empty structs are
     rejected for now.
   - Arrays: elements are u8/u16/u32, or a struct only in `S[..]` (a list),
     and that struct must not itself run to the end of the message.
   - Length fields: `d: u8[n]` needs `n` to be an earlier, non-constant
     integer field of the same struct, of any width, and `n` may size only
     one array. Arrays sized by a field must be `u8`.
   - Anything that runs to the end of the message must be last: a `[..]`
     field, a field of a struct that ends that way, or a switch with such
     an arm (including `_`).
   - Switches: the tag is an earlier field of enum type; arm tags are
     variants of it and appear once; arm types are structs; `_` stands
     alone, is the last arm and has type `u8[..]`.
   - C names: field names are not C keywords; distinct schema names stay
     distinct after snake_casing, and don't take names the generated code
     uses itself (`<proto>_status`, `_cstr`, `_bytes`, `_rd`, `_wr`,
     `<S>_list`, `<S>_iter`, `<struct>_<switch>`, `body.unknown`, and
     constants such as `<PROTO>_OK`, `_CHECK` and `<MSG>_MAX_SIZE`).
3. **Lower to an IR (done):** `ir.py`. A flat list of wire operations per
   struct (`Group` of `Bits`, `Cstr`, `Rest`, `Bytes`, `Fixed`, `Pad`,
   `Sub`, `List`, `Dispatch`), with every name already in C form, and
   structs ordered dependencies first. Decode, encode and size are all
   generated from the same list, so they cannot disagree. Fixed offsets
   are not computed; the constant parts of a size are folded
   (`2 + in->data.len`).
4. **Emit (done):** `emit.py`. Python f-strings for code that depends on
   the schema, and `string.Template` (`${p}` = prefix) for fixed C such as
   the primitives and list functions. Signatures that depend on name
   lengths are wrapped by one helper, so all output stays within 79
   columns. Long schema comments wrap one field per line.

`python3 pktc.py SCHEMA [-o OUTDIR] [--fuzz]` runs all four stages and
writes `<proto>_gen.h` and `<proto>_gen.c` (next to the schema by
default). `--fuzz` also writes `<proto>_fuzz.c`, a libFuzzer harness (see
"Testing").

## Testing

- **Golden files:** the generated code and fuzz harness in
  `examples/tftp` and `examples/pcp` are committed; `tests/test_emit.py`
  regenerates them and fails if they differ. After changing the emitter,
  regenerate with `python3 pktc.py examples/<proto>/<proto>.schema --fuzz`
  and review the diff.
- **Example tests:** `test_tftp.c` and `test_pcp.c` (PCP packets laid out
  from RFC 6887) are compiled with `-Werror` and sanitizers and run by
  `tests/test_emit.py`.
- **Generality:** `tests/test_emit.py` also generates two made-up
  protocols and round-trips packets through them: `demo` (u8/u32, u32
  enums, nested structs, switches in non-message structs, shared arms,
  several messages) and `bits` (a constant, enum, length and flags in one
  16-bit group, `u24`, `u16[3]`, reserved arrays, `pad(8)` mid-message, a
  computed `MAX_SIZE`).
- **Round trip:** decode then encode gives back the same bytes, except for
  reserved fields and padding, which come back as zeros.
- **Fuzzing (libFuzzer via clang):** the harness is generated
  (`pktc.py --fuzz`), so every schema gets one. Each input is used twice:
  - *As a packet:* decode it; touch every view, list element and union arm
    so ASan checks them; it must re-encode to its own length, and fail with
    `NOSPACE` one byte short. Messages with no reserved fields or padding
    must re-encode to identical bytes. Messages with them must decode
    again and then re-encode identically, since reserved bytes and padding
    come back as zeros.
  - *As a recipe:* generated `build_<struct>` functions read choices from
    it (tags, list counts, view lengths, field values masked to their
    widths) and build a valid message. It must encode to exactly
    `encoded_size` bytes (or be `INVALID` only when over a declared `max`),
    decode, and re-encode identically. Decoding random bytes alone rarely
    gets past length fields and padding: in a test with a bug planted in
    PCP option padding, decode-only fuzzing missed it for minutes, while
    the recipe side found it in about 4,000 runs.

  `tests/test_emit.py` generates and runs the harness for TFTP, PCP,
  `demo` and `bits` for 3 seconds each when clang is available. For
  longer runs, build the committed `examples/<proto>/<proto>_fuzz.c` with
  the command in its header comment.

## Next steps

1. ~~Hand-write `examples/tftp/tftp_gen.c` to match `tftp_gen.h`, with a
   round-trip test and a fuzz harness.~~ Done: `tftp_gen.c` is the
   emitter's target output; `test_tftp.c` holds the round-trip and edge-case
   tests. The hand-written fuzz harness was later replaced by a generated
   one (`tftp_fuzz.c`).
2. ~~Write the checker.~~ Done: `check.py`, tests in `tests/test_check.py`.
3. ~~Write the IR and C emitter, and make it reproduce the hand-written TFTP
   code.~~ Done: `ir.py`, `emit.py`, `pktc.py`. The committed TFTP code is
   now generated. The C tests and fuzzer pass against it unchanged, except
   that `encoded_size` no longer promises 0 for an unknown tag.
4. ~~Add PCP: bitfields, constant fields, fixed arrays, length fields,
   padding and the `_` fallback arm, plus `const`, `max` and the size
   constants.~~ Done: `examples/pcp/`.
5. Candidates for next, roughly in order:
   - Design the escape hatch, which STUN needs (XOR-mapped addresses,
     CRC fingerprint).
   - Add NTP or STUN as a third protocol. STUN would also force a decision
     on byte-length-prefixed lists (see "Deferred").
   - Emit a standalone Lark parser so the compiler has no dependencies
     either.
   - Add a Makefile (or similar) for the C tests and long fuzz runs.
