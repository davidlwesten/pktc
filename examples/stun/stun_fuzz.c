/* Generated from stun.pkt -- do not edit.
 *
 * libFuzzer harness for the code in stun_gen.c. Each input is used twice:
 * as a packet to decode (decode_<message>), and as a recipe for building
 * a valid message to encode (encode_<message>).
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       stun_fuzz.c stun_gen.c -o stun_fuzz
 */
#include <stdlib.h>
#include <string.h>

#include "stun_gen.h"

/* Recipe input: choices are read from [p, end); past the end, they are 0. */
struct src {
    const uint8_t *p;
    const uint8_t *end;
};

static uint8_t take_u8(struct src *in) {
    return in->p < in->end ? *in->p++ : 0;
}

static uint32_t take_u32(struct src *in) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v = v << 8 | take_u8(in);
    return v;
}

static void take_raw(struct src *in, uint8_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = take_u8(in);
}

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

static volatile uint8_t sink;

static void walk_bytes(const struct stun_bytes *v) {
    if (v->len != 0)
        sink = v->ptr[v->len - 1];
}

/* A view into the recipe itself: up to max bytes, as many as remain. */
static void take_bytes(struct src *in, struct stun_bytes *v, size_t max) {
    size_t n = (size_t)take_u8(in) << 8 | take_u8(in);
    size_t left = (size_t)(in->end - in->p);
    n = n % (max < SIZE_MAX ? max + 1 : SIZE_MAX);
    v->len = n < left ? n : left;
    v->ptr = in->p;
    in->p += v->len;
}

static void walk_attribute(const struct stun_attribute *v) {
    walk_bytes(&v->value);
}

static void walk_message(const struct stun_message *v) {
    {
        struct stun_attribute_iter it;
        struct stun_attribute item;
        stun_attribute_iter_init(&it, &v->attributes);
        while (stun_attribute_iter_next(&it, &item))
            walk_attribute(&item);
    }
}

static void walk_ipv4(const struct stun_ipv4 *v) {
    (void)v;
}

static void walk_ipv6(const struct stun_ipv6 *v) {
    (void)v;
}

static void walk_address(const struct stun_address *v) {
    switch (v->family) {
    case STUN_FAMILY_IPV4:
        walk_ipv4(&v->address.ipv4);
        break;
    case STUN_FAMILY_IPV6:
        walk_ipv6(&v->address.ipv6);
        break;
    default:
        break;
    }
}

static void walk_error_code(const struct stun_error_code *v) {
    walk_bytes(&v->reason);
}

static void walk_fingerprint(const struct stun_fingerprint *v) {
    (void)v;
}

static void build_attribute(struct src *in, struct stun_attribute *v) {
    v->type = (enum stun_attribute_type)(take_u32(in) & 0xffff);
    take_bytes(in, &v->value, 0xffff);
}

static void build_message(struct src *in, struct stun_message *v) {
    v->type = (enum stun_type)(take_u32(in) & 0x3fff);
    take_raw(in, v->transaction_id, 12);
    {
        size_t n = take_u8(in) % 4;
        struct stun_attribute *items = arena_alloc(n * sizeof *items);
        if (items == NULL)
            n = 0;
        for (size_t i = 0; i < n; i++)
            build_attribute(in, &items[i]);
        struct stun_attribute_list list = { .items = items, .count = n };
        v->attributes = list;
    }
}

static void build_ipv4(struct src *in, struct stun_ipv4 *v) {
    take_raw(in, v->addr, 4);
}

static void build_ipv6(struct src *in, struct stun_ipv6 *v) {
    take_raw(in, v->addr, 16);
}

static void build_address(struct src *in, struct stun_address *v) {
    v->family = (enum stun_family)(take_u32(in) & 0xff);
    v->port = (uint16_t)(take_u32(in) & 0xffff);
    switch (take_u8(in) % 2) {
    case 0:
        v->family = STUN_FAMILY_IPV4;
        build_ipv4(in, &v->address.ipv4);
        break;
    default:
        v->family = STUN_FAMILY_IPV6;
        build_ipv6(in, &v->address.ipv6);
        break;
    }
}

static void build_error_code(struct src *in, struct stun_error_code *v) {
    v->class = (uint8_t)(take_u32(in) & 0x7);
    v->number = (uint8_t)(take_u32(in) & 0xff);
    take_bytes(in, &v->reason, SIZE_MAX);
}

static void build_fingerprint(struct src *in, struct stun_fingerprint *v) {
    v->crc = (uint32_t)(take_u32(in) & 0xffffffff);
}

/* Decode then encode must give back the input exactly. */
static void decode_message(const uint8_t *buf, size_t size) {
    struct stun_message msg;
    if (stun_message_decode(&msg, buf, size) != STUN_OK)
        return;
    walk_message(&msg);
    if (stun_message_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (stun_message_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    if (size != 0 &&
        stun_message_encode(&msg, b, size - 1, &n) != STUN_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Message from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_message(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct stun_message msg = {};
    arena_used = 0;
    build_message(&in, &msg);
    size_t size = stun_message_encoded_size(&msg);
    size_t n = 0;
    if (size > STUN_MESSAGE_MAX_SIZE) {
        if (stun_message_encode(&msg, NULL, 0, &n) != STUN_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (stun_message_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    struct stun_message back;
    if (stun_message_decode(&back, a, size) != STUN_OK)
        abort();
    if (stun_message_encode(&back, b, size, &n) != STUN_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Address has reserved fields or padding, which decode ignores and
 * encode writes as zeros, so the first encode may differ from the
 * input there. It must still have the same length, decode again, and
 * encode to identical bytes. */
static void decode_address(const uint8_t *buf, size_t size) {
    struct stun_address msg;
    if (stun_address_decode(&msg, buf, size) != STUN_OK)
        return;
    walk_address(&msg);
    if (stun_address_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (stun_address_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    if (size != 0 &&
        stun_address_encode(&msg, b, size - 1, &n) != STUN_NOSPACE)
        abort();
    struct stun_address again;
    if (stun_address_decode(&again, a, size) != STUN_OK)
        abort();
    if (stun_address_encode(&again, b, size, &n) != STUN_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Address from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_address(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct stun_address msg = {};
    arena_used = 0;
    build_address(&in, &msg);
    size_t size = stun_address_encoded_size(&msg);
    size_t n = 0;
    if (size > STUN_ADDRESS_MAX_SIZE) {
        if (stun_address_encode(&msg, NULL, 0, &n) != STUN_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (stun_address_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    struct stun_address back;
    if (stun_address_decode(&back, a, size) != STUN_OK)
        abort();
    if (stun_address_encode(&back, b, size, &n) != STUN_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* ErrorCode has reserved fields or padding, which decode ignores and
 * encode writes as zeros, so the first encode may differ from the
 * input there. It must still have the same length, decode again, and
 * encode to identical bytes. */
static void decode_error_code(const uint8_t *buf, size_t size) {
    struct stun_error_code msg;
    if (stun_error_code_decode(&msg, buf, size) != STUN_OK)
        return;
    walk_error_code(&msg);
    if (stun_error_code_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (stun_error_code_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    if (size != 0 &&
        stun_error_code_encode(&msg, b, size - 1, &n) != STUN_NOSPACE)
        abort();
    struct stun_error_code again;
    if (stun_error_code_decode(&again, a, size) != STUN_OK)
        abort();
    if (stun_error_code_encode(&again, b, size, &n) != STUN_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid ErrorCode from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_error_code(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct stun_error_code msg = {};
    arena_used = 0;
    build_error_code(&in, &msg);
    size_t size = stun_error_code_encoded_size(&msg);
    size_t n = 0;
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (stun_error_code_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    struct stun_error_code back;
    if (stun_error_code_decode(&back, a, size) != STUN_OK)
        abort();
    if (stun_error_code_encode(&back, b, size, &n) != STUN_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Decode then encode must give back the input exactly. */
static void decode_fingerprint(const uint8_t *buf, size_t size) {
    struct stun_fingerprint msg;
    if (stun_fingerprint_decode(&msg, buf, size) != STUN_OK)
        return;
    walk_fingerprint(&msg);
    if (stun_fingerprint_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (stun_fingerprint_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    if (size != 0 &&
        stun_fingerprint_encode(&msg, b, size - 1, &n) != STUN_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Fingerprint from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_fingerprint(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct stun_fingerprint msg = {};
    arena_used = 0;
    build_fingerprint(&in, &msg);
    size_t size = stun_fingerprint_encoded_size(&msg);
    size_t n = 0;
    if (size > STUN_FINGERPRINT_MAX_SIZE) {
        if (stun_fingerprint_encode(&msg, NULL, 0, &n) != STUN_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (stun_fingerprint_encode(&msg, a, size, &n) != STUN_OK || n != size)
        abort();
    struct stun_fingerprint back;
    if (stun_fingerprint_decode(&back, a, size) != STUN_OK)
        abort();
    if (stun_fingerprint_encode(&back, b, size, &n) != STUN_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* An exact-size heap copy, so ASan catches any overread. */
    uint8_t *buf = malloc(size != 0 ? size : 1);
    if (size != 0)
        memcpy(buf, data, size);
    decode_message(buf, size);
    encode_message(buf, size);
    decode_address(buf, size);
    encode_address(buf, size);
    decode_error_code(buf, size);
    encode_error_code(buf, size);
    decode_fingerprint(buf, size);
    encode_fingerprint(buf, size);
    free(buf);
    return 0;
}
