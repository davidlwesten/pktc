/* Tests for the STUN codec, using the test vectors of RFC 5769.
 *
 * XOR-MAPPED-ADDRESS and FINGERPRINT are not wire format: they are
 * computed from decoded values or encoded bytes. The helpers below show
 * how a caller handles them. (MESSAGE-INTEGRITY works like FINGERPRINT,
 * with HMAC-SHA1 in place of CRC-32.) */
#include <stdio.h>
#include <string.h>

#include "stun_gen.h"

static int failures;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, \
                    #cond);                                          \
            failures++;                                              \
        }                                                            \
    } while (0)

/* ---- caller-side helpers ---------------------------------------------- */

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (0xedb88320 & -(c & 1));
    }
    return ~c;
}

/* Section 14.7: FINGERPRINT must be last, and covers everything before
 * its own attribute header. */
static bool fingerprint_ok(const uint8_t *buf, const struct stun_message *m) {
    struct stun_attribute_iter it;
    struct stun_attribute a;
    stun_attribute_iter_init(&it, &m->attributes);
    while (stun_attribute_iter_next(&it, &a)) {
        if (a.type != STUN_ATTRIBUTE_TYPE_FINGERPRINT)
            continue;
        struct stun_fingerprint fp;
        if (stun_fingerprint_decode(&fp, a.value.ptr, a.value.len) != STUN_OK)
            return false;
        size_t off = (size_t)(a.value.ptr - buf) - 4;
        return !stun_attribute_iter_next(&it, &a) &&
               fp.crc == (crc32(buf, off) ^ 0x5354554e);
    }
    return false;
}

/* Section 14.2: undo (or apply) the XOR of an XOR-MAPPED-ADDRESS. */
static void xor_address(struct stun_address *a, const uint8_t txid[12]) {
    uint8_t key[16] = {
        STUN_MAGIC_COOKIE >> 24, STUN_MAGIC_COOKIE >> 16 & 0xff,
        STUN_MAGIC_COOKIE >> 8 & 0xff, STUN_MAGIC_COOKIE & 0xff,
    };
    memcpy(key + 4, txid, 12);
    a->port ^= STUN_MAGIC_COOKIE >> 16;
    if (a->family == STUN_FAMILY_IPV4)
        for (int i = 0; i < 4; i++)
            a->address.ipv4.addr[i] ^= key[i];
    else
        for (int i = 0; i < 16; i++)
            a->address.ipv6.addr[i] ^= key[i];
}

/* The attribute of the given type, or false. */
static bool find(const struct stun_message *m, uint16_t type,
                 struct stun_attribute *out) {
    struct stun_attribute_iter it;
    stun_attribute_iter_init(&it, &m->attributes);
    while (stun_attribute_iter_next(&it, out))
        if (out->type == type)
            return true;
    return false;
}

/* ---- RFC 5769 test vectors (padding is spaces in the first three) ----- */

static const uint8_t req[] =
    "\x00\x01\x00\x58"
    "\x21\x12\xa4\x42"
    "\xb7\xe7\xa7\x01\xbc\x34\xd6\x86\xfa\x87\xdf\xae"
    "\x80\x22\x00\x10"
    "STUN test client"
    "\x00\x24\x00\x04"
    "\x6e\x00\x01\xff"
    "\x80\x29\x00\x08"
    "\x93\x2f\xf9\xb1\x51\x26\x3b\x36"
    "\x00\x06\x00\x09"
    "\x65\x76\x74\x6a\x3a\x68\x36\x76\x59\x20\x20\x20"
    "\x00\x08\x00\x14"
    "\x9a\xea\xa7\x0c\xbf\xd8\xcb\x56\x78\x1e\xf2\xb5"
    "\xb2\xd3\xf2\x49\xc1\xb5\x71\xa2"
    "\x80\x28\x00\x04"
    "\xe5\x7a\x3b\xcf";

static const uint8_t respv4[] =
    "\x01\x01\x00\x3c"
    "\x21\x12\xa4\x42"
    "\xb7\xe7\xa7\x01\xbc\x34\xd6\x86\xfa\x87\xdf\xae"
    "\x80\x22\x00\x0b"
    "\x74\x65\x73\x74\x20\x76\x65\x63\x74\x6f\x72\x20"
    "\x00\x20\x00\x08"
    "\x00\x01\xa1\x47\xe1\x12\xa6\x43"
    "\x00\x08\x00\x14"
    "\x2b\x91\xf5\x99\xfd\x9e\x90\xc3\x8c\x74\x89\xf9"
    "\x2a\xf9\xba\x53\xf0\x6b\xe7\xd7"
    "\x80\x28\x00\x04"
    "\xc0\x7d\x4c\x96";

static const uint8_t respv6[] =
    "\x01\x01\x00\x48"
    "\x21\x12\xa4\x42"
    "\xb7\xe7\xa7\x01\xbc\x34\xd6\x86\xfa\x87\xdf\xae"
    "\x80\x22\x00\x0b"
    "\x74\x65\x73\x74\x20\x76\x65\x63\x74\x6f\x72\x20"
    "\x00\x20\x00\x14"
    "\x00\x02\xa1\x47"
    "\x01\x13\xa9\xfa\xa5\xd3\xf1\x79"
    "\xbc\x25\xf4\xb5\xbe\xd2\xb9\xd9"
    "\x00\x08\x00\x14"
    "\xa3\x82\x95\x4e\x4b\xe6\x7b\xf1\x17\x84\xc9\x7c"
    "\x82\x92\xc2\x75\xbf\xe3\xed\x41"
    "\x80\x28\x00\x04"
    "\xc8\xfb\x0b\x4c";

static const uint8_t reqltc[] =
    "\x00\x01\x00\x60"
    "\x21\x12\xa4\x42"
    "\x78\xad\x34\x33\xc6\xad\x72\xc0\x29\xda\x41\x2e"
    "\x00\x06\x00\x12"
    "\xe3\x83\x9e\xe3\x83\x88\xe3\x83\xaa\xe3\x83\x83"
    "\xe3\x82\xaf\xe3\x82\xb9\x00\x00"
    "\x00\x15\x00\x1c"
    "\x66\x2f\x2f\x34\x39\x39\x6b\x39\x35\x34\x64\x36"
    "\x4f\x4c\x33\x34\x6f\x4c\x39\x46\x53\x54\x76\x79"
    "\x36\x34\x73\x41"
    "\x00\x14\x00\x0b"
    "\x65\x78\x61\x6d\x70\x6c\x65\x2e\x6f\x72\x67\x00"
    "\x00\x08\x00\x14"
    "\xf6\x70\x24\x65\x6d\xd6\x4a\x3e\x02\xb8\xe0\x71"
    "\x2e\x85\xc9\xa2\x8c\xa8\x96\x66";

/* Each vector's length, without the string literal's NUL. */
#define LEN(v) (sizeof(v) - 1)

/* Decode then encode gives back the same bytes, padding included: a
 * decoded attribute list is re-encoded from its raw bytes. (A list built
 * from items is written with zero padding; see test_encode_response.) */
static void round_trip(const uint8_t *buf, size_t len) {
    struct stun_message m;
    CHECK(stun_message_decode(&m, buf, len) == STUN_OK);
    CHECK(stun_message_encoded_size(&m) == len);
    uint8_t out[128];
    size_t n = 0;
    CHECK(stun_message_encode(&m, out, sizeof out, &n) == STUN_OK);
    CHECK(n == len && memcmp(out, buf, len) == 0);
    CHECK(stun_message_encode(&m, out, len - 1, &n) == STUN_NOSPACE);
}

static void test_request(void) {
    struct stun_message m;
    CHECK(stun_message_decode(&m, req, LEN(req)) == STUN_OK);
    CHECK(m.type == STUN_TYPE_BINDING_REQUEST);
    CHECK(m.transaction_id[0] == 0xb7 && m.transaction_id[11] == 0xae);

    static const struct { uint16_t type; size_t len; } want[] = {
        { STUN_ATTRIBUTE_TYPE_SOFTWARE, 16 },
        { 0x0024, 4 },  /* PRIORITY (ICE, RFC 8445): not in the enum */
        { 0x8029, 8 },  /* ICE-CONTROLLED */
        { STUN_ATTRIBUTE_TYPE_USERNAME, 9 },
        { STUN_ATTRIBUTE_TYPE_MESSAGE_INTEGRITY, 20 },
        { STUN_ATTRIBUTE_TYPE_FINGERPRINT, 4 },
    };
    struct stun_attribute_iter it;
    struct stun_attribute a;
    stun_attribute_iter_init(&it, &m.attributes);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK(stun_attribute_iter_next(&it, &a));
        CHECK(a.type == want[i].type && a.value.len == want[i].len);
    }
    CHECK(!stun_attribute_iter_next(&it, &a));

    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_USERNAME, &a));
    CHECK(memcmp(a.value.ptr, "evtj:h6vY", 9) == 0);
    CHECK(fingerprint_ok(req, &m));

    round_trip(req, LEN(req));
}

static void test_responses(void) {
    struct stun_message m;
    struct stun_attribute a;
    struct stun_address addr;

    CHECK(stun_message_decode(&m, respv4, LEN(respv4)) == STUN_OK);
    CHECK(m.type == STUN_TYPE_BINDING_SUCCESS);
    CHECK(fingerprint_ok(respv4, &m));
    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_SOFTWARE, &a));
    CHECK(a.value.len == 11 && memcmp(a.value.ptr, "test vector", 11) == 0);
    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_XOR_MAPPED_ADDRESS, &a));
    CHECK(stun_address_decode(&addr, a.value.ptr, a.value.len) == STUN_OK);
    xor_address(&addr, m.transaction_id);
    CHECK(addr.family == STUN_FAMILY_IPV4 && addr.port == 32853);
    CHECK(memcmp(addr.address.ipv4.addr, "\xc0\x00\x02\x01", 4) == 0);
    round_trip(respv4, LEN(respv4));

    CHECK(stun_message_decode(&m, respv6, LEN(respv6)) == STUN_OK);
    CHECK(fingerprint_ok(respv6, &m));
    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_XOR_MAPPED_ADDRESS, &a));
    CHECK(stun_address_decode(&addr, a.value.ptr, a.value.len) == STUN_OK);
    xor_address(&addr, m.transaction_id);
    static const uint8_t ip6[16] = {
        0x20, 0x01, 0x0d, 0xb8, 0x12, 0x34, 0x56, 0x78,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    };
    CHECK(addr.family == STUN_FAMILY_IPV6 && addr.port == 32853);
    CHECK(memcmp(addr.address.ipv6.addr, ip6, 16) == 0);
    round_trip(respv6, LEN(respv6));
}

static void test_long_term(void) {
    struct stun_message m;
    struct stun_attribute a;
    CHECK(stun_message_decode(&m, reqltc, LEN(reqltc)) == STUN_OK);
    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_USERNAME, &a) && a.value.len == 18);
    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_NONCE, &a) && a.value.len == 28);
    CHECK(find(&m, STUN_ATTRIBUTE_TYPE_REALM, &a) && a.value.len == 11);
    CHECK(memcmp(a.value.ptr, "example.org", 11) == 0);
    round_trip(reqltc, LEN(reqltc));
}

static void test_framing(void) {
    struct stun_message m;
    uint8_t buf[sizeof respv4];
    size_t len = LEN(respv4);

    CHECK(stun_message_decode(&m, respv4, len - 1) == STUN_INVALID);
    memcpy(buf, respv4, len);
    buf[len] = 0;
    CHECK(stun_message_decode(&m, buf, len + 1) == STUN_INVALID);  /* extra */

    memcpy(buf, respv4, len);
    buf[3] = 0x38;  /* length ends inside the FINGERPRINT attribute */
    CHECK(stun_message_decode(&m, buf, len) == STUN_INVALID);
    CHECK(stun_message_decode(&m, buf, len - 4) == STUN_INVALID);

    memcpy(buf, respv4, len);
    buf[3] = 0x34;  /* length ends before FINGERPRINT: trailing bytes */
    CHECK(stun_message_decode(&m, buf, len) == STUN_INVALID);
    CHECK(stun_message_decode(&m, buf, len - 8) == STUN_OK);

    memcpy(buf, respv4, len);
    buf[0] |= 0x40;  /* top bits must be zero */
    CHECK(stun_message_decode(&m, buf, len) == STUN_INVALID);

    memcpy(buf, respv4, len);
    buf[7] ^= 1;  /* not the magic cookie */
    CHECK(stun_message_decode(&m, buf, len) == STUN_INVALID);

    /* An empty Binding request is just the header. */
    static const uint8_t empty[20] = { 0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42 };
    CHECK(stun_message_decode(&m, empty, sizeof empty) == STUN_OK);
    CHECK(m.attributes.raw_len == 0);
}

static void test_encode_response(void) {
    /* A Binding success response for 192.0.2.1:32853 with a FINGERPRINT.
     * Attribute values are encoded first, then the message, then the
     * fingerprint is computed and written into its placeholder. */
    struct stun_address addr = {
        .family = STUN_FAMILY_IPV4,
        .port = 32853,
        .address.ipv4.addr = { 192, 0, 2, 1 },
    };
    struct stun_message m = {
        .type = STUN_TYPE_BINDING_SUCCESS,
        .transaction_id = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 },
    };
    xor_address(&addr, m.transaction_id);
    uint8_t xaddr[STUN_ADDRESS_MAX_SIZE];
    size_t xlen = 0;
    CHECK(stun_address_encode(&addr, xaddr, sizeof xaddr, &xlen) == STUN_OK);

    static const uint8_t zero[4];
    const struct stun_attribute attrs[] = {
        { STUN_ATTRIBUTE_TYPE_SOFTWARE, { (const uint8_t *)"pktc", 4 } },
        { STUN_ATTRIBUTE_TYPE_XOR_MAPPED_ADDRESS, { xaddr, xlen } },
        { STUN_ATTRIBUTE_TYPE_USERNAME, { (const uint8_t *)"abcde", 5 } },
        { STUN_ATTRIBUTE_TYPE_FINGERPRINT, { zero, 4 } },
    };
    m.attributes = (struct stun_attribute_list){ .items = attrs, .count = 4 };

    uint8_t out[128];
    size_t n = 0;
    CHECK(stun_message_encoded_size(&m) == 20 + 8 + 12 + 12 + 8);
    CHECK(stun_message_encode(&m, out, sizeof out, &n) == STUN_OK);
    CHECK(n == 60 && out[2] == 0 && out[3] == 40);  /* length excludes header */
    CHECK(memcmp(out + 4, "\x21\x12\xa4\x42", 4) == 0);
    CHECK(memcmp(out + 44, "abcde\0\0\0", 8) == 0);  /* zero padding */

    struct stun_fingerprint fp = { crc32(out, n - 8) ^ 0x5354554e };
    size_t fn = 0;
    CHECK(stun_fingerprint_encode(&fp, out + n - 4, 4, &fn) == STUN_OK);

    struct stun_message back;
    struct stun_attribute a;
    CHECK(stun_message_decode(&back, out, n) == STUN_OK);
    CHECK(fingerprint_ok(out, &back));
    CHECK(find(&back, STUN_ATTRIBUTE_TYPE_XOR_MAPPED_ADDRESS, &a));
    struct stun_address got;
    CHECK(stun_address_decode(&got, a.value.ptr, a.value.len) == STUN_OK);
    xor_address(&got, back.transaction_id);
    CHECK(got.port == 32853 && got.address.ipv4.addr[0] == 192);

    /* Attributes too long for the 16-bit length. */
    static uint8_t big[65528];
    const struct stun_attribute huge[] = {
        { STUN_ATTRIBUTE_TYPE_SOFTWARE, { big, sizeof big } },
        { STUN_ATTRIBUTE_TYPE_USERNAME, { big, 1 } },
    };
    static uint8_t bigout[65600];
    m.attributes = (struct stun_attribute_list){ .items = huge, .count = 1 };
    CHECK(stun_message_encode(&m, bigout, sizeof bigout, &n) == STUN_OK);
    CHECK(n == 20 + 4 + sizeof big);  /* length 65532 */
    m.attributes.count = 2;  /* 65532 + 8 > 65535 */
    CHECK(stun_message_encode(&m, bigout, sizeof bigout, &n) == STUN_INVALID);
}

static void test_values(void) {
    /* ERROR-CODE 420: class 4, number 20. */
    struct stun_error_code e = {
        .class = 4, .number = 20,
        .reason = { (const uint8_t *)"Unknown Attribute", 17 },
    };
    uint8_t out[32];
    size_t n = 0;
    CHECK(stun_error_code_encode(&e, out, sizeof out, &n) == STUN_OK);
    CHECK(n == 21 && memcmp(out, "\0\0\x04\x14Unknown", 11) == 0);
    struct stun_error_code back;
    CHECK(stun_error_code_decode(&back, out, n) == STUN_OK);
    CHECK(back.class * 100 + back.number == 420 && back.reason.len == 17);

    /* Reserved bits are ignored. */
    out[0] = 0xff;
    out[2] = 0xfc;
    CHECK(stun_error_code_decode(&back, out, n) == STUN_OK && back.class == 4);

    /* The family selects the address length. */
    static const uint8_t v4_as_v6[] = { 0, 2, 0, 80, 192, 0, 2, 1 };
    struct stun_address a;
    CHECK(stun_address_decode(&a, v4_as_v6, sizeof v4_as_v6) == STUN_INVALID);
    static const uint8_t family3[] = { 0, 3, 0, 80, 192, 0, 2, 1 };
    CHECK(stun_address_decode(&a, family3, sizeof family3) == STUN_INVALID);
}

int main(void) {
    test_request();
    test_responses();
    test_long_term();
    test_framing();
    test_encode_response();
    test_values();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
