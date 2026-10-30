/* Generated from pcp.pkt -- do not edit.
 *
 * libFuzzer harness for the code in pcp_gen.c. Each input is used twice:
 * as a packet to decode (decode_<message>), and as a recipe for building
 * a valid message to encode (encode_<message>).
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       pcp_fuzz.c pcp_gen.c -o pcp_fuzz
 */
#include <stdlib.h>
#include <string.h>

#include "pcp_gen.h"

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

static void walk_bytes(const struct pcp_bytes *v) {
    if (v->len != 0)
        sink = v->ptr[v->len - 1];
}

/* A view into the recipe itself: up to max bytes, as many as remain. */
static void take_bytes(struct src *in, struct pcp_bytes *v, size_t max) {
    size_t n = (size_t)take_u8(in) << 8 | take_u8(in);
    size_t left = (size_t)(in->end - in->p);
    n = n % (max < SIZE_MAX ? max + 1 : SIZE_MAX);
    v->len = n < left ? n : left;
    v->ptr = in->p;
    in->p += v->len;
}

static void walk_option(const struct pcp_option *v) {
    walk_bytes(&v->data);
}

static void walk_announce(const struct pcp_announce *v) {
    {
        struct pcp_option_iter it;
        struct pcp_option item;
        pcp_option_iter_init(&it, &v->options);
        while (pcp_option_iter_next(&it, &item))
            walk_option(&item);
    }
}

static void walk_map(const struct pcp_map *v) {
    {
        struct pcp_option_iter it;
        struct pcp_option item;
        pcp_option_iter_init(&it, &v->options);
        while (pcp_option_iter_next(&it, &item))
            walk_option(&item);
    }
}

static void walk_peer(const struct pcp_peer *v) {
    {
        struct pcp_option_iter it;
        struct pcp_option item;
        pcp_option_iter_init(&it, &v->options);
        while (pcp_option_iter_next(&it, &item))
            walk_option(&item);
    }
}

static void walk_request(const struct pcp_request *v) {
    switch (v->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        walk_announce(&v->body.announce);
        break;
    case PCP_OPCODE_MAP:
        walk_map(&v->body.map);
        break;
    case PCP_OPCODE_PEER:
        walk_peer(&v->body.peer);
        break;
    default:
        walk_bytes(&v->body.unknown);
        break;
    }
}

static void walk_response(const struct pcp_response *v) {
    switch (v->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        walk_announce(&v->body.announce);
        break;
    case PCP_OPCODE_MAP:
        walk_map(&v->body.map);
        break;
    case PCP_OPCODE_PEER:
        walk_peer(&v->body.peer);
        break;
    default:
        walk_bytes(&v->body.unknown);
        break;
    }
}

static void build_option(struct src *in, struct pcp_option *v) {
    v->code = (enum pcp_option_code)(take_u32(in) & 0xff);
    take_bytes(in, &v->data, 0xffff);
}

static void build_announce(struct src *in, struct pcp_announce *v) {
    {
        size_t n = take_u8(in) % 4;
        struct pcp_option *items = arena_alloc(n * sizeof *items);
        if (items == NULL)
            n = 0;
        for (size_t i = 0; i < n; i++)
            build_option(in, &items[i]);
        struct pcp_option_list list = { .items = items, .count = n };
        v->options = list;
    }
}

static void build_map(struct src *in, struct pcp_map *v) {
    take_raw(in, v->nonce, 12);
    v->protocol = (uint8_t)(take_u32(in) & 0xff);
    v->internal_port = (uint16_t)(take_u32(in) & 0xffff);
    v->external_port = (uint16_t)(take_u32(in) & 0xffff);
    take_raw(in, v->external_ip, 16);
    {
        size_t n = take_u8(in) % 4;
        struct pcp_option *items = arena_alloc(n * sizeof *items);
        if (items == NULL)
            n = 0;
        for (size_t i = 0; i < n; i++)
            build_option(in, &items[i]);
        struct pcp_option_list list = { .items = items, .count = n };
        v->options = list;
    }
}

static void build_peer(struct src *in, struct pcp_peer *v) {
    take_raw(in, v->nonce, 12);
    v->protocol = (uint8_t)(take_u32(in) & 0xff);
    v->internal_port = (uint16_t)(take_u32(in) & 0xffff);
    v->external_port = (uint16_t)(take_u32(in) & 0xffff);
    take_raw(in, v->external_ip, 16);
    v->remote_port = (uint16_t)(take_u32(in) & 0xffff);
    take_raw(in, v->remote_ip, 16);
    {
        size_t n = take_u8(in) % 4;
        struct pcp_option *items = arena_alloc(n * sizeof *items);
        if (items == NULL)
            n = 0;
        for (size_t i = 0; i < n; i++)
            build_option(in, &items[i]);
        struct pcp_option_list list = { .items = items, .count = n };
        v->options = list;
    }
}

static void build_request(struct src *in, struct pcp_request *v) {
    v->version = (uint8_t)(take_u32(in) & 0xff);
    v->opcode = (enum pcp_opcode)(take_u32(in) & 0x7f);
    v->lifetime = (uint32_t)(take_u32(in) & 0xffffffff);
    take_raw(in, v->client_ip, 16);
    switch (take_u8(in) % 4) {
    case 0:
        v->opcode = PCP_OPCODE_ANNOUNCE;
        build_announce(in, &v->body.announce);
        break;
    case 1:
        v->opcode = PCP_OPCODE_MAP;
        build_map(in, &v->body.map);
        break;
    case 2:
        v->opcode = PCP_OPCODE_PEER;
        build_peer(in, &v->body.peer);
        break;
    default:
        v->opcode = (enum pcp_opcode)3;  /* no arm */
        take_bytes(in, &v->body.unknown, SIZE_MAX);
        break;
    }
}

static void build_response(struct src *in, struct pcp_response *v) {
    v->version = (uint8_t)(take_u32(in) & 0xff);
    v->opcode = (enum pcp_opcode)(take_u32(in) & 0x7f);
    v->result = (enum pcp_result)(take_u32(in) & 0xff);
    v->lifetime = (uint32_t)(take_u32(in) & 0xffffffff);
    v->epoch = (uint32_t)(take_u32(in) & 0xffffffff);
    switch (take_u8(in) % 4) {
    case 0:
        v->opcode = PCP_OPCODE_ANNOUNCE;
        build_announce(in, &v->body.announce);
        break;
    case 1:
        v->opcode = PCP_OPCODE_MAP;
        build_map(in, &v->body.map);
        break;
    case 2:
        v->opcode = PCP_OPCODE_PEER;
        build_peer(in, &v->body.peer);
        break;
    default:
        v->opcode = (enum pcp_opcode)3;  /* no arm */
        take_bytes(in, &v->body.unknown, SIZE_MAX);
        break;
    }
}

/* Request has reserved fields or padding, which decode ignores and
 * encode writes as zeros, so the first encode may differ from the
 * input there. It must still have the same length, decode again, and
 * encode to identical bytes. */
static void decode_request(const uint8_t *buf, size_t size) {
    struct pcp_request msg;
    if (pcp_request_decode(&msg, buf, size) != PCP_OK)
        return;
    walk_request(&msg);
    if (pcp_request_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (pcp_request_encode(&msg, a, size, &n) != PCP_OK || n != size)
        abort();
    if (size != 0 &&
        pcp_request_encode(&msg, b, size - 1, &n) != PCP_NOSPACE)
        abort();
    struct pcp_request again;
    if (pcp_request_decode(&again, a, size) != PCP_OK)
        abort();
    if (pcp_request_encode(&again, b, size, &n) != PCP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Request from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_request(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct pcp_request msg = {};
    arena_used = 0;
    build_request(&in, &msg);
    size_t size = pcp_request_encoded_size(&msg);
    size_t n = 0;
    if (size > PCP_REQUEST_MAX_SIZE) {
        if (pcp_request_encode(&msg, NULL, 0, &n) != PCP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (pcp_request_encode(&msg, a, size, &n) != PCP_OK || n != size)
        abort();
    struct pcp_request back;
    if (pcp_request_decode(&back, a, size) != PCP_OK)
        abort();
    if (pcp_request_encode(&back, b, size, &n) != PCP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Response has reserved fields or padding, which decode ignores and
 * encode writes as zeros, so the first encode may differ from the
 * input there. It must still have the same length, decode again, and
 * encode to identical bytes. */
static void decode_response(const uint8_t *buf, size_t size) {
    struct pcp_response msg;
    if (pcp_response_decode(&msg, buf, size) != PCP_OK)
        return;
    walk_response(&msg);
    if (pcp_response_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (pcp_response_encode(&msg, a, size, &n) != PCP_OK || n != size)
        abort();
    if (size != 0 &&
        pcp_response_encode(&msg, b, size - 1, &n) != PCP_NOSPACE)
        abort();
    struct pcp_response again;
    if (pcp_response_decode(&again, a, size) != PCP_OK)
        abort();
    if (pcp_response_encode(&again, b, size, &n) != PCP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Response from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_response(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct pcp_response msg = {};
    arena_used = 0;
    build_response(&in, &msg);
    size_t size = pcp_response_encoded_size(&msg);
    size_t n = 0;
    if (size > PCP_RESPONSE_MAX_SIZE) {
        if (pcp_response_encode(&msg, NULL, 0, &n) != PCP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (pcp_response_encode(&msg, a, size, &n) != PCP_OK || n != size)
        abort();
    struct pcp_response back;
    if (pcp_response_decode(&back, a, size) != PCP_OK)
        abort();
    if (pcp_response_encode(&back, b, size, &n) != PCP_OK || n != size ||
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
    decode_request(buf, size);
    encode_request(buf, size);
    decode_response(buf, size);
    encode_response(buf, size);
    free(buf);
    return 0;
}
