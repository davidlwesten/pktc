/* Generated from ntp.pkt -- do not edit.
 *
 * libFuzzer harness for the code in ntp_gen.c. Each input is used twice:
 * as a packet to decode (decode_<message>), and as a recipe for building
 * a valid message to encode (encode_<message>).
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       ntp_fuzz.c ntp_gen.c -o ntp_fuzz
 */
#include <stdlib.h>
#include <string.h>

#include "ntp_gen.h"

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

static volatile uint8_t sink;

static void walk_bytes(const struct ntp_bytes *v) {
    if (v->len != 0)
        sink = v->ptr[v->len - 1];
}

/* A view into the recipe itself: up to max bytes, as many as remain. */
static void take_bytes(struct src *in, struct ntp_bytes *v, size_t max) {
    size_t n = (size_t)take_u8(in) << 8 | take_u8(in);
    size_t left = (size_t)(in->end - in->p);
    n = n % (max < SIZE_MAX ? max + 1 : SIZE_MAX);
    v->len = n < left ? n : left;
    v->ptr = in->p;
    in->p += v->len;
}

static void walk_short(const struct ntp_short *v) {
    (void)v;
}

static void walk_timestamp(const struct ntp_timestamp *v) {
    (void)v;
}

static void walk_time(const struct ntp_time *v) {
    walk_short(&v->root_delay);
    walk_short(&v->root_dispersion);
    walk_timestamp(&v->reference);
    walk_timestamp(&v->origin);
    walk_timestamp(&v->receive);
    walk_timestamp(&v->transmit);
    walk_bytes(&v->extensions);
}

static void walk_control(const struct ntp_control *v) {
    walk_bytes(&v->data);
    walk_bytes(&v->authenticator);
}

static void walk_packet(const struct ntp_packet *v) {
    switch (v->mode) {
    case NTP_MODE_SYMMETRIC_ACTIVE:
    case NTP_MODE_SYMMETRIC_PASSIVE:
    case NTP_MODE_CLIENT:
    case NTP_MODE_SERVER:
    case NTP_MODE_BROADCAST:
        walk_time(&v->body.time);
        break;
    case NTP_MODE_CONTROL:
        walk_control(&v->body.control);
        break;
    default:
        walk_bytes(&v->body.unknown);
        break;
    }
}

static void build_short(struct src *in, struct ntp_short *v) {
    v->seconds = (uint16_t)(take_u32(in) & 0xffff);
    v->fraction = (uint16_t)(take_u32(in) & 0xffff);
}

static void build_timestamp(struct src *in, struct ntp_timestamp *v) {
    v->seconds = (uint32_t)(take_u32(in) & 0xffffffff);
    v->fraction = (uint32_t)(take_u32(in) & 0xffffffff);
}

static void build_time(struct src *in, struct ntp_time *v) {
    v->stratum = (uint8_t)(take_u32(in) & 0xff);
    v->poll = (int8_t)((int64_t)(take_u32(in) & 0xff) - 128);
    v->precision = (int8_t)((int64_t)(take_u32(in) & 0xff) - 128);
    build_short(in, &v->root_delay);
    build_short(in, &v->root_dispersion);
    take_raw(in, v->reference_id, 4);
    build_timestamp(in, &v->reference);
    build_timestamp(in, &v->origin);
    build_timestamp(in, &v->receive);
    build_timestamp(in, &v->transmit);
    take_bytes(in, &v->extensions, SIZE_MAX);
}

static void build_control(struct src *in, struct ntp_control *v) {
    v->response = (uint8_t)(take_u32(in) & 0x1);
    v->error = (uint8_t)(take_u32(in) & 0x1);
    v->more = (uint8_t)(take_u32(in) & 0x1);
    v->opcode = (enum ntp_control_op)(take_u32(in) & 0x1f);
    v->sequence = (uint16_t)(take_u32(in) & 0xffff);
    v->status = (uint16_t)(take_u32(in) & 0xffff);
    v->association_id = (uint16_t)(take_u32(in) & 0xffff);
    v->offset = (uint16_t)(take_u32(in) & 0xffff);
    take_bytes(in, &v->data, 0xffff);
    take_bytes(in, &v->authenticator, SIZE_MAX);
}

static void build_packet(struct src *in, struct ntp_packet *v) {
    v->leap = (enum ntp_leap)(take_u32(in) & 0x3);
    v->version = (uint8_t)(take_u32(in) & 0x7);
    v->mode = (enum ntp_mode)(take_u32(in) & 0x7);
    switch (take_u8(in) % 7) {
    case 0:
        v->mode = NTP_MODE_SYMMETRIC_ACTIVE;
        build_time(in, &v->body.time);
        break;
    case 1:
        v->mode = NTP_MODE_SYMMETRIC_PASSIVE;
        build_time(in, &v->body.time);
        break;
    case 2:
        v->mode = NTP_MODE_CLIENT;
        build_time(in, &v->body.time);
        break;
    case 3:
        v->mode = NTP_MODE_SERVER;
        build_time(in, &v->body.time);
        break;
    case 4:
        v->mode = NTP_MODE_BROADCAST;
        build_time(in, &v->body.time);
        break;
    case 5:
        v->mode = NTP_MODE_CONTROL;
        build_control(in, &v->body.control);
        break;
    default:
        v->mode = (enum ntp_mode)0;  /* no arm */
        take_bytes(in, &v->body.unknown, SIZE_MAX);
        break;
    }
}

/* Packet has reserved fields or padding, which decode ignores and
 * encode writes as zeros, so the first encode may differ from the
 * input there. It must still have the same length, decode again, and
 * encode to identical bytes. */
static void decode_packet(const uint8_t *buf, size_t size) {
    struct ntp_packet msg;
    if (ntp_packet_decode(&msg, buf, size) != NTP_OK)
        return;
    walk_packet(&msg);
    if (ntp_packet_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (ntp_packet_encode(&msg, a, size, &n) != NTP_OK || n != size)
        abort();
    if (size != 0 &&
        ntp_packet_encode(&msg, b, size - 1, &n) != NTP_NOSPACE)
        abort();
    struct ntp_packet again;
    if (ntp_packet_decode(&again, a, size) != NTP_OK)
        abort();
    if (ntp_packet_encode(&again, b, size, &n) != NTP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid Packet from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_packet(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct ntp_packet msg = {};
    build_packet(&in, &msg);
    size_t size = ntp_packet_encoded_size(&msg);
    size_t n = 0;
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (ntp_packet_encode(&msg, a, size, &n) != NTP_OK || n != size)
        abort();
    struct ntp_packet back;
    if (ntp_packet_decode(&back, a, size) != NTP_OK)
        abort();
    if (ntp_packet_encode(&back, b, size, &n) != NTP_OK || n != size ||
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
