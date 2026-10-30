/* Generated from ntp.pkt -- do not edit. */
#include "ntp_gen.h"

#include <string.h>

/* ---- wire primitives ---------------------------------------------------- */

/* Read cursor over [p, end). Every read checks bounds first. */
struct ntp_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct ntp_wr {
    uint8_t *p;
    uint8_t *end;
};

static size_t ntp_rd_left(const struct ntp_rd *r) {
    return (size_t)(r->end - r->p);
}

static bool ntp_rd_u8(struct ntp_rd *r, uint8_t *v) {
    if (ntp_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}

static bool ntp_rd_u16(struct ntp_rd *r, uint16_t *v) {
    if (ntp_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}

static bool ntp_rd_u32(struct ntp_rd *r, uint32_t *v) {
    if (ntp_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}

static bool ntp_rd_skip(struct ntp_rd *r, size_t n) {
    if (ntp_rd_left(r) < n)
        return false;
    r->p += n;
    return true;
}

static bool ntp_rd_raw(struct ntp_rd *r, uint8_t *dst, size_t n) {
    if (ntp_rd_left(r) < n)
        return false;
    memcpy(dst, r->p, n);
    r->p += n;
    return true;
}

static bool ntp_rd_rest(struct ntp_rd *r, struct ntp_bytes *v) {
    v->ptr = r->p;
    v->len = ntp_rd_left(r);
    r->p = r->end;
    return true;
}

static bool ntp_rd_bytes(struct ntp_rd *r, struct ntp_bytes *v, size_t n) {
    if (ntp_rd_left(r) < n)
        return false;
    v->ptr = r->p;
    v->len = n;
    r->p += n;
    return true;
}

static size_t ntp_wr_left(const struct ntp_wr *w) {
    return (size_t)(w->end - w->p);
}

static ntp_status ntp_wr_u8(struct ntp_wr *w, uint8_t v) {
    if (ntp_wr_left(w) < 1)
        return NTP_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return NTP_OK;
}

static ntp_status ntp_wr_u16(struct ntp_wr *w, uint16_t v) {
    if (ntp_wr_left(w) < 2)
        return NTP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return NTP_OK;
}

static ntp_status ntp_wr_u32(struct ntp_wr *w, uint32_t v) {
    if (ntp_wr_left(w) < 4)
        return NTP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 24);
    w->p[1] = (uint8_t)(v >> 16);
    w->p[2] = (uint8_t)(v >> 8);
    w->p[3] = (uint8_t)v;
    w->p += 4;
    return NTP_OK;
}

static ntp_status ntp_wr_raw(struct ntp_wr *w, const void *src, size_t n) {
    if (ntp_wr_left(w) < n)
        return NTP_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return NTP_OK;
}

static ntp_status ntp_wr_zero(struct ntp_wr *w, size_t n) {
    if (ntp_wr_left(w) < n)
        return NTP_NOSPACE;
    if (n != 0)
        memset(w->p, 0, n);
    w->p += n;
    return NTP_OK;
}

/* The low `bits` bits of v as a two's complement integer. */
static int32_t ntp_sext(uint32_t v, unsigned bits) {
    uint32_t sign = (uint32_t)1 << (bits - 1);
    uint32_t mask = sign * 2 - 1;  /* wraps to all ones for 32 bits */
    return (v & sign) != 0 ? -(int32_t)(~v & mask) - 1 : (int32_t)(v & mask);
}

/* Zero bytes that pad n bytes to a multiple of align. */
static size_t ntp_pad_len(size_t n, size_t align) {
    return (align - n % align) % align;
}

#define NTP_CHECK(cond) \
    do { \
        if (!(cond)) \
            return false; \
    } while (0)

#define NTP_TRY(expr) \
    do { \
        ntp_status st_ = (expr); \
        if (st_ != NTP_OK) \
            return st_; \
    } while (0)

/* ---- struct Short ------------------------------------------------------- */

static bool ntp_short_read(struct ntp_rd *r, struct ntp_short *out) {
    NTP_CHECK(ntp_rd_u16(r, &out->seconds));
    return ntp_rd_u16(r, &out->fraction);
}

static ntp_status ntp_short_write(struct ntp_wr *w,
                                  const struct ntp_short *in) {
    NTP_TRY(ntp_wr_u16(w, in->seconds));
    return ntp_wr_u16(w, in->fraction);
}

static size_t ntp_short_size(const struct ntp_short *in) {
    (void)in;
    return 4;
}

/* ---- struct Timestamp --------------------------------------------------- */

static bool ntp_timestamp_read(struct ntp_rd *r, struct ntp_timestamp *out) {
    NTP_CHECK(ntp_rd_u32(r, &out->seconds));
    return ntp_rd_u32(r, &out->fraction);
}

static ntp_status ntp_timestamp_write(struct ntp_wr *w,
                                      const struct ntp_timestamp *in) {
    NTP_TRY(ntp_wr_u32(w, in->seconds));
    return ntp_wr_u32(w, in->fraction);
}

static size_t ntp_timestamp_size(const struct ntp_timestamp *in) {
    (void)in;
    return 8;
}

/* ---- struct Time -------------------------------------------------------- */

static bool ntp_time_read(struct ntp_rd *r, struct ntp_time *out) {
    NTP_CHECK(ntp_rd_u8(r, &out->stratum));
    uint8_t bits1;
    NTP_CHECK(ntp_rd_u8(r, &bits1));
    out->poll = (int8_t)ntp_sext(bits1, 8);
    uint8_t bits2;
    NTP_CHECK(ntp_rd_u8(r, &bits2));
    out->precision = (int8_t)ntp_sext(bits2, 8);
    NTP_CHECK(ntp_short_read(r, &out->root_delay));
    NTP_CHECK(ntp_short_read(r, &out->root_dispersion));
    NTP_CHECK(ntp_rd_raw(r, out->reference_id, 4));
    NTP_CHECK(ntp_timestamp_read(r, &out->reference));
    NTP_CHECK(ntp_timestamp_read(r, &out->origin));
    NTP_CHECK(ntp_timestamp_read(r, &out->receive));
    NTP_CHECK(ntp_timestamp_read(r, &out->transmit));
    return ntp_rd_rest(r, &out->extensions);
}

static ntp_status ntp_time_write(struct ntp_wr *w, const struct ntp_time *in) {
    NTP_TRY(ntp_wr_u8(w, in->stratum));
    NTP_TRY(ntp_wr_u8(w, (uint8_t)in->poll));
    NTP_TRY(ntp_wr_u8(w, (uint8_t)in->precision));
    NTP_TRY(ntp_short_write(w, &in->root_delay));
    NTP_TRY(ntp_short_write(w, &in->root_dispersion));
    NTP_TRY(ntp_wr_raw(w, in->reference_id, 4));
    NTP_TRY(ntp_timestamp_write(w, &in->reference));
    NTP_TRY(ntp_timestamp_write(w, &in->origin));
    NTP_TRY(ntp_timestamp_write(w, &in->receive));
    NTP_TRY(ntp_timestamp_write(w, &in->transmit));
    return ntp_wr_raw(w, in->extensions.ptr, in->extensions.len);
}

static size_t ntp_time_size(const struct ntp_time *in) {
    return 7
         + ntp_short_size(&in->root_delay)
         + ntp_short_size(&in->root_dispersion)
         + ntp_timestamp_size(&in->reference)
         + ntp_timestamp_size(&in->origin)
         + ntp_timestamp_size(&in->receive)
         + ntp_timestamp_size(&in->transmit)
         + in->extensions.len;
}

/* ---- struct Control ----------------------------------------------------- */

static bool ntp_control_read(struct ntp_rd *r, struct ntp_control *out) {
    uint8_t bits0;
    NTP_CHECK(ntp_rd_u8(r, &bits0));
    out->response = (uint8_t)(bits0 >> 7);
    out->error = (uint8_t)((bits0 >> 6) & 0x1);
    out->more = (uint8_t)((bits0 >> 5) & 0x1);
    out->opcode = (enum ntp_control_op)(bits0 & 0x1f);
    NTP_CHECK(ntp_rd_u16(r, &out->sequence));
    NTP_CHECK(ntp_rd_u16(r, &out->status));
    NTP_CHECK(ntp_rd_u16(r, &out->association_id));
    NTP_CHECK(ntp_rd_u16(r, &out->offset));
    uint16_t data_len;
    NTP_CHECK(ntp_rd_u16(r, &data_len));
    NTP_CHECK(ntp_rd_bytes(r, &out->data, data_len));
    NTP_CHECK(ntp_rd_skip(r, ntp_pad_len(data_len, 4)));
    return ntp_rd_rest(r, &out->authenticator);
}

static ntp_status ntp_control_write(struct ntp_wr *w,
                                    const struct ntp_control *in) {
    if (in->response > 0x1)
        return NTP_INVALID;
    if (in->error > 0x1)
        return NTP_INVALID;
    if (in->more > 0x1)
        return NTP_INVALID;
    if (in->opcode > 0x1f)
        return NTP_INVALID;
    NTP_TRY(ntp_wr_u8(w, (uint8_t)(((uint8_t)in->response << 7) |
                                   ((uint8_t)in->error << 6) |
                                   ((uint8_t)in->more << 5) | in->opcode)));
    NTP_TRY(ntp_wr_u16(w, in->sequence));
    NTP_TRY(ntp_wr_u16(w, in->status));
    NTP_TRY(ntp_wr_u16(w, in->association_id));
    NTP_TRY(ntp_wr_u16(w, in->offset));
    if (in->data.len > 0xffff)
        return NTP_INVALID;
    NTP_TRY(ntp_wr_u16(w, (uint16_t)(in->data.len)));
    NTP_TRY(ntp_wr_raw(w, in->data.ptr, in->data.len));
    NTP_TRY(ntp_wr_zero(w, ntp_pad_len(in->data.len, 4)));
    return ntp_wr_raw(w, in->authenticator.ptr, in->authenticator.len);
}

static size_t ntp_control_size(const struct ntp_control *in) {
    return 11
         + in->data.len
         + ntp_pad_len(in->data.len, 4)
         + in->authenticator.len;
}

/* ---- message Packet ----------------------------------------------------- */

static bool ntp_packet_body_read(struct ntp_rd *r, struct ntp_packet *out) {
    switch (out->mode) {
    case NTP_MODE_SYMMETRIC_ACTIVE:
    case NTP_MODE_SYMMETRIC_PASSIVE:
    case NTP_MODE_CLIENT:
    case NTP_MODE_SERVER:
    case NTP_MODE_BROADCAST:
        return ntp_time_read(r, &out->body.time);
    case NTP_MODE_CONTROL:
        return ntp_control_read(r, &out->body.control);
    default:
        return ntp_rd_rest(r, &out->body.unknown);
    }
}

static ntp_status ntp_packet_body_write(struct ntp_wr *w,
                                        const struct ntp_packet *in) {
    switch (in->mode) {
    case NTP_MODE_SYMMETRIC_ACTIVE:
    case NTP_MODE_SYMMETRIC_PASSIVE:
    case NTP_MODE_CLIENT:
    case NTP_MODE_SERVER:
    case NTP_MODE_BROADCAST:
        return ntp_time_write(w, &in->body.time);
    case NTP_MODE_CONTROL:
        return ntp_control_write(w, &in->body.control);
    default:
        return ntp_wr_raw(w, in->body.unknown.ptr, in->body.unknown.len);
    }
}

static size_t ntp_packet_body_size(const struct ntp_packet *in) {
    switch (in->mode) {
    case NTP_MODE_SYMMETRIC_ACTIVE:
    case NTP_MODE_SYMMETRIC_PASSIVE:
    case NTP_MODE_CLIENT:
    case NTP_MODE_SERVER:
    case NTP_MODE_BROADCAST:
        return ntp_time_size(&in->body.time);
    case NTP_MODE_CONTROL:
        return ntp_control_size(&in->body.control);
    default:
        return in->body.unknown.len;
    }
}

static bool ntp_packet_read(struct ntp_rd *r, struct ntp_packet *out) {
    uint8_t bits0;
    NTP_CHECK(ntp_rd_u8(r, &bits0));
    out->leap = (enum ntp_leap)(bits0 >> 6);
    out->version = (uint8_t)((bits0 >> 3) & 0x7);
    out->mode = (enum ntp_mode)(bits0 & 0x7);
    return ntp_packet_body_read(r, out);
}

static ntp_status ntp_packet_write(struct ntp_wr *w,
                                   const struct ntp_packet *in) {
    if (in->leap > 0x3)
        return NTP_INVALID;
    if (in->version > 0x7)
        return NTP_INVALID;
    if (in->mode > 0x7)
        return NTP_INVALID;
    NTP_TRY(ntp_wr_u8(w, (uint8_t)(((uint8_t)in->leap << 6) |
                                   ((uint8_t)in->version << 3) | in->mode)));
    return ntp_packet_body_write(w, in);
}

static size_t ntp_packet_size(const struct ntp_packet *in) {
    return 1 + ntp_packet_body_size(in);
}

ntp_status ntp_packet_decode(struct ntp_packet *out, const uint8_t *buf,
                             size_t len) {
    struct ntp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!ntp_packet_read(&r, out) || r.p != r.end)
        return NTP_INVALID;
    return NTP_OK;
}

ntp_status ntp_packet_encode(const struct ntp_packet *in, uint8_t *buf,
                             size_t cap, size_t *written) {
    struct ntp_wr w = { buf, cap != 0 ? buf + cap : buf };
    NTP_TRY(ntp_packet_write(&w, in));
    *written = (size_t)(w.p - buf);
    return NTP_OK;
}

size_t ntp_packet_encoded_size(const struct ntp_packet *in) {
    return ntp_packet_size(in);
}
