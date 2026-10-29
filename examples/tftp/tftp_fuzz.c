/* Generated from tftp.schema -- do not edit.
 *
 * libFuzzer harness for the code in tftp_gen.c. Each input is used twice:
 * as a packet to decode (decode_<message>), and as a recipe for building
 * a valid message to encode (encode_<message>).
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       tftp_fuzz.c tftp_gen.c -o tftp_fuzz
 */
#include <stdlib.h>
#include <string.h>

#include "tftp_gen.h"

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

static void walk_bytes(const struct tftp_bytes *v) {
    if (v->len != 0)
        sink = v->ptr[v->len - 1];
}

/* A view into the recipe itself: up to max bytes, as many as remain. */
static void take_bytes(struct src *in, struct tftp_bytes *v, size_t max) {
    size_t n = (size_t)take_u8(in) << 8 | take_u8(in);
    size_t left = (size_t)(in->end - in->p);
    n = n % (max < SIZE_MAX ? max + 1 : SIZE_MAX);
    v->len = n < left ? n : left;
    v->ptr = in->p;
    in->p += v->len;
}

static void walk_cstr(const struct tftp_cstr *v) {
    if (v->ptr[v->len] != '\0')
        abort();
}

/* A short string without NULs, copied into the arena. */
static void take_cstr(struct src *in, struct tftp_cstr *v) {
    size_t n = take_u8(in) % 16;
    char *s = arena_alloc(n + 1);
    if (s == NULL)
        n = 0, s = "";
    for (size_t i = 0; i < n; i++) {
        uint8_t c = take_u8(in);
        s[i] = (char)(c != 0 ? c : 'x');
    }
    v->ptr = s;
    v->len = n;
}

static void walk_option(const struct tftp_option *v) {
    walk_cstr(&v->name);
    walk_cstr(&v->value);
}

static void walk_request(const struct tftp_request *v) {
    walk_cstr(&v->filename);
    walk_cstr(&v->mode);
    {
        struct tftp_option_iter it;
        struct tftp_option item;
        tftp_option_iter_init(&it, &v->options);
        while (tftp_option_iter_next(&it, &item))
            walk_option(&item);
    }
}

static void walk_data(const struct tftp_data *v) {
    walk_bytes(&v->data);
}

static void walk_ack(const struct tftp_ack *v) {
    (void)v;
}

static void walk_error(const struct tftp_error *v) {
    walk_cstr(&v->message);
}

static void walk_oack(const struct tftp_oack *v) {
    {
        struct tftp_option_iter it;
        struct tftp_option item;
        tftp_option_iter_init(&it, &v->options);
        while (tftp_option_iter_next(&it, &item))
            walk_option(&item);
    }
}

static void walk_packet(const struct tftp_packet *v) {
    switch (v->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        walk_request(&v->body.request);
        break;
    case TFTP_OPCODE_DATA:
        walk_data(&v->body.data);
        break;
    case TFTP_OPCODE_ACK:
        walk_ack(&v->body.ack);
        break;
    case TFTP_OPCODE_ERROR:
        walk_error(&v->body.error);
        break;
    case TFTP_OPCODE_OACK:
        walk_oack(&v->body.oack);
        break;
    default:
        break;
    }
}

static void build_option(struct src *in, struct tftp_option *v) {
    take_cstr(in, &v->name);
    take_cstr(in, &v->value);
}

static void build_request(struct src *in, struct tftp_request *v) {
    take_cstr(in, &v->filename);
    take_cstr(in, &v->mode);
    {
        size_t n = take_u8(in) % 4;
        struct tftp_option *items = arena_alloc(n * sizeof *items);
        if (items == NULL)
            n = 0;
        for (size_t i = 0; i < n; i++)
            build_option(in, &items[i]);
        struct tftp_option_list list = { .items = items, .count = n };
        v->options = list;
    }
}

static void build_data(struct src *in, struct tftp_data *v) {
    v->block = (uint16_t)(take_u32(in) & 0xffff);
    take_bytes(in, &v->data, SIZE_MAX);
}

static void build_ack(struct src *in, struct tftp_ack *v) {
    v->block = (uint16_t)(take_u32(in) & 0xffff);
}

static void build_error(struct src *in, struct tftp_error *v) {
    v->code = (uint16_t)(take_u32(in) & 0xffff);
    take_cstr(in, &v->message);
}

static void build_oack(struct src *in, struct tftp_oack *v) {
    {
        size_t n = take_u8(in) % 4;
        struct tftp_option *items = arena_alloc(n * sizeof *items);
        if (items == NULL)
            n = 0;
        for (size_t i = 0; i < n; i++)
            build_option(in, &items[i]);
        struct tftp_option_list list = { .items = items, .count = n };
        v->options = list;
    }
}

static void build_packet(struct src *in, struct tftp_packet *v) {
    v->opcode = (enum tftp_opcode)(take_u32(in) & 0xffff);
    switch (take_u8(in) % 6) {
    case 0:
        v->opcode = TFTP_OPCODE_RRQ;
        build_request(in, &v->body.request);
        break;
    case 1:
        v->opcode = TFTP_OPCODE_WRQ;
        build_request(in, &v->body.request);
        break;
    case 2:
        v->opcode = TFTP_OPCODE_DATA;
        build_data(in, &v->body.data);
        break;
    case 3:
        v->opcode = TFTP_OPCODE_ACK;
        build_ack(in, &v->body.ack);
        break;
    case 4:
        v->opcode = TFTP_OPCODE_ERROR;
        build_error(in, &v->body.error);
        break;
    default:
        v->opcode = TFTP_OPCODE_OACK;
        build_oack(in, &v->body.oack);
        break;
    }
}

/* Decode then encode must give back the input exactly. */
static void decode_packet(const uint8_t *buf, size_t size) {
    struct tftp_packet msg;
    if (tftp_packet_decode(&msg, buf, size) != TFTP_OK)
        return;
    walk_packet(&msg);
    if (tftp_packet_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (tftp_packet_encode(&msg, a, size, &n) != TFTP_OK || n != size)
        abort();
    if (size != 0 &&
        tftp_packet_encode(&msg, b, size - 1, &n) != TFTP_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Packet from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_packet(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct tftp_packet msg = {};
    arena_used = 0;
    build_packet(&in, &msg);
    size_t size = tftp_packet_encoded_size(&msg);
    size_t n = 0;
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (tftp_packet_encode(&msg, a, size, &n) != TFTP_OK || n != size)
        abort();
    struct tftp_packet back;
    if (tftp_packet_decode(&back, a, size) != TFTP_OK)
        abort();
    if (tftp_packet_encode(&back, b, size, &n) != TFTP_OK || n != size ||
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
    decode_packet(buf, size);
    encode_packet(buf, size);
    free(buf);
    return 0;
}
