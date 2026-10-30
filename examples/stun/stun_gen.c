/* Generated from stun.pkt -- do not edit. */
#include "stun_gen.h"

#include <string.h>

/* ---- wire primitives ---------------------------------------------------- */

/* Read cursor over [p, end). Every read checks bounds first. */
struct stun_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct stun_wr {
    uint8_t *p;
    uint8_t *end;
};

static size_t stun_rd_left(const struct stun_rd *r) {
    return (size_t)(r->end - r->p);
}

static bool stun_rd_u8(struct stun_rd *r, uint8_t *v) {
    if (stun_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}

static bool stun_rd_u16(struct stun_rd *r, uint16_t *v) {
    if (stun_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}

static bool stun_rd_u24(struct stun_rd *r, uint32_t *v) {
    if (stun_rd_left(r) < 3)
        return false;
    *v = (uint32_t)r->p[0] << 16 | (uint32_t)r->p[1] << 8 | r->p[2];
    r->p += 3;
    return true;
}

static bool stun_rd_u32(struct stun_rd *r, uint32_t *v) {
    if (stun_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}

static bool stun_rd_skip(struct stun_rd *r, size_t n) {
    if (stun_rd_left(r) < n)
        return false;
    r->p += n;
    return true;
}

static bool stun_rd_raw(struct stun_rd *r, uint8_t *dst, size_t n) {
    if (stun_rd_left(r) < n)
        return false;
    memcpy(dst, r->p, n);
    r->p += n;
    return true;
}

/* Splits the next n bytes off r into sub. */
static bool stun_rd_sub(struct stun_rd *r, struct stun_rd *sub, size_t n) {
    if (stun_rd_left(r) < n)
        return false;
    sub->p = r->p;
    sub->end = r->p + n;
    r->p += n;
    return true;
}

static bool stun_rd_rest(struct stun_rd *r, struct stun_bytes *v) {
    v->ptr = r->p;
    v->len = stun_rd_left(r);
    r->p = r->end;
    return true;
}

static bool stun_rd_bytes(struct stun_rd *r, struct stun_bytes *v, size_t n) {
    if (stun_rd_left(r) < n)
        return false;
    v->ptr = r->p;
    v->len = n;
    r->p += n;
    return true;
}

static size_t stun_wr_left(const struct stun_wr *w) {
    return (size_t)(w->end - w->p);
}

static stun_status stun_wr_u8(struct stun_wr *w, uint8_t v) {
    if (stun_wr_left(w) < 1)
        return STUN_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return STUN_OK;
}

static stun_status stun_wr_u16(struct stun_wr *w, uint16_t v) {
    if (stun_wr_left(w) < 2)
        return STUN_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return STUN_OK;
}

static stun_status stun_wr_u24(struct stun_wr *w, uint32_t v) {
    if (stun_wr_left(w) < 3)
        return STUN_NOSPACE;
    w->p[0] = (uint8_t)(v >> 16);
    w->p[1] = (uint8_t)(v >> 8);
    w->p[2] = (uint8_t)v;
    w->p += 3;
    return STUN_OK;
}

static stun_status stun_wr_u32(struct stun_wr *w, uint32_t v) {
    if (stun_wr_left(w) < 4)
        return STUN_NOSPACE;
    w->p[0] = (uint8_t)(v >> 24);
    w->p[1] = (uint8_t)(v >> 16);
    w->p[2] = (uint8_t)(v >> 8);
    w->p[3] = (uint8_t)v;
    w->p += 4;
    return STUN_OK;
}

static stun_status stun_wr_raw(struct stun_wr *w, const void *src, size_t n) {
    if (stun_wr_left(w) < n)
        return STUN_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return STUN_OK;
}

static stun_status stun_wr_zero(struct stun_wr *w, size_t n) {
    if (stun_wr_left(w) < n)
        return STUN_NOSPACE;
    if (n != 0)
        memset(w->p, 0, n);
    w->p += n;
    return STUN_OK;
}

/* Zero bytes that pad n bytes to a multiple of align. */
static size_t stun_pad_len(size_t n, size_t align) {
    return (align - n % align) % align;
}

#define STUN_CHECK(cond) \
    do { \
        if (!(cond)) \
            return false; \
    } while (0)

#define STUN_TRY(expr) \
    do { \
        stun_status st_ = (expr); \
        if (st_ != STUN_OK) \
            return st_; \
    } while (0)

/* ---- struct Attribute --------------------------------------------------- */

static bool stun_attribute_read(struct stun_rd *r,
                                struct stun_attribute *out) {
    STUN_CHECK(stun_rd_u16(r, &out->type));
    uint16_t value_len;
    STUN_CHECK(stun_rd_u16(r, &value_len));
    STUN_CHECK(stun_rd_bytes(r, &out->value, value_len));
    return stun_rd_skip(r, stun_pad_len(value_len, 4));
}

static stun_status stun_attribute_write(struct stun_wr *w,
                                        const struct stun_attribute *in) {
    STUN_TRY(stun_wr_u16(w, in->type));
    if (in->value.len > 0xffff)
        return STUN_INVALID;
    STUN_TRY(stun_wr_u16(w, (uint16_t)(in->value.len)));
    STUN_TRY(stun_wr_raw(w, in->value.ptr, in->value.len));
    return stun_wr_zero(w, stun_pad_len(in->value.len, 4));
}

static size_t stun_attribute_size(const struct stun_attribute *in) {
    return 4 + in->value.len + stun_pad_len(in->value.len, 4);
}

/* ---- Attribute[..] ------------------------------------------------------ */

/* Consumes the rest of r. Fails unless it is a whole number of Attributes. */
static bool stun_attribute_list_read(struct stun_rd *r,
                                     struct stun_attribute_list *out) {
    const uint8_t *start = r->p;
    struct stun_attribute tmp;
    while (r->p != r->end)
        if (!stun_attribute_read(r, &tmp))
            return false;
    out->raw = start;
    out->raw_len = (size_t)(r->p - start);
    out->items = NULL;
    out->count = 0;
    return true;
}

static stun_status stun_attribute_list_write(
    struct stun_wr *w, const struct stun_attribute_list *in) {
    if (in->items != NULL) {
        for (size_t i = 0; i < in->count; i++)
            STUN_TRY(stun_attribute_write(w, &in->items[i]));
        return STUN_OK;
    }
    if (in->raw_len == 0)
        return STUN_OK;
    /* raw may have been filled in by hand: encode never emits bytes that
     * decode would reject. */
    struct stun_rd r = { in->raw, in->raw + in->raw_len };
    struct stun_attribute_list check;
    if (!stun_attribute_list_read(&r, &check))
        return STUN_INVALID;
    return stun_wr_raw(w, in->raw, in->raw_len);
}

static size_t stun_attribute_list_size(const struct stun_attribute_list *in) {
    if (in->items == NULL)
        return in->raw_len;
    size_t n = 0;
    for (size_t i = 0; i < in->count; i++)
        n += stun_attribute_size(&in->items[i]);
    return n;
}

void stun_attribute_iter_init(struct stun_attribute_iter *it,
                              const struct stun_attribute_list *list) {
    it->pos = list->raw;
    it->end = list->raw_len != 0 ? list->raw + list->raw_len : list->raw;
}

bool stun_attribute_iter_next(struct stun_attribute_iter *it,
                              struct stun_attribute *out) {
    if (it->pos == it->end)
        return false;
    struct stun_rd r = { it->pos, it->end };
    if (!stun_attribute_read(&r, out))
        return false;
    it->pos = r.p;
    return true;
}

/* ---- message Message ---------------------------------------------------- */

static bool stun_message_read(struct stun_rd *r, struct stun_message *out) {
    uint16_t bits0;
    STUN_CHECK(stun_rd_u16(r, &bits0));
    STUN_CHECK((bits0 >> 14) == 0);
    out->type = (enum stun_type)(bits0 & 0x3fff);
    uint16_t attributes_len;
    STUN_CHECK(stun_rd_u16(r, &attributes_len));
    uint32_t bits2;
    STUN_CHECK(stun_rd_u32(r, &bits2));
    STUN_CHECK(bits2 == 0x2112a442);
    STUN_CHECK(stun_rd_raw(r, out->transaction_id, 12));
    struct stun_rd attributes_r;
    STUN_CHECK(stun_rd_sub(r, &attributes_r, attributes_len));
    return stun_attribute_list_read(&attributes_r, &out->attributes);
}

static stun_status stun_message_write(struct stun_wr *w,
                                      const struct stun_message *in) {
    if (in->type > 0x3fff)
        return STUN_INVALID;
    STUN_TRY(stun_wr_u16(w, (uint16_t)(in->type)));
    size_t attributes_len = stun_attribute_list_size(&in->attributes);
    if (attributes_len > 0xffff)
        return STUN_INVALID;
    STUN_TRY(stun_wr_u16(w, (uint16_t)(attributes_len)));
    STUN_TRY(stun_wr_u32(w, (uint32_t)(0x2112a442)));
    STUN_TRY(stun_wr_raw(w, in->transaction_id, 12));
    return stun_attribute_list_write(w, &in->attributes);
}

static size_t stun_message_size(const struct stun_message *in) {
    return 20 + stun_attribute_list_size(&in->attributes);
}

stun_status stun_message_decode(struct stun_message *out, const uint8_t *buf,
                                size_t len) {
    struct stun_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!stun_message_read(&r, out) || r.p != r.end)
        return STUN_INVALID;
    return STUN_OK;
}

stun_status stun_message_encode(const struct stun_message *in, uint8_t *buf,
                                size_t cap, size_t *written) {
    struct stun_wr w = { buf, cap != 0 ? buf + cap : buf };
    STUN_TRY(stun_message_write(&w, in));
    *written = (size_t)(w.p - buf);
    return STUN_OK;
}

size_t stun_message_encoded_size(const struct stun_message *in) {
    return stun_message_size(in);
}

/* ---- struct Ipv4 -------------------------------------------------------- */

static bool stun_ipv4_read(struct stun_rd *r, struct stun_ipv4 *out) {
    return stun_rd_raw(r, out->addr, 4);
}

static stun_status stun_ipv4_write(struct stun_wr *w,
                                   const struct stun_ipv4 *in) {
    return stun_wr_raw(w, in->addr, 4);
}

static size_t stun_ipv4_size(const struct stun_ipv4 *in) {
    (void)in;
    return 4;
}

/* ---- struct Ipv6 -------------------------------------------------------- */

static bool stun_ipv6_read(struct stun_rd *r, struct stun_ipv6 *out) {
    return stun_rd_raw(r, out->addr, 16);
}

static stun_status stun_ipv6_write(struct stun_wr *w,
                                   const struct stun_ipv6 *in) {
    return stun_wr_raw(w, in->addr, 16);
}

static size_t stun_ipv6_size(const struct stun_ipv6 *in) {
    (void)in;
    return 16;
}

/* ---- message Address ---------------------------------------------------- */

static bool stun_address_address_read(struct stun_rd *r,
                                      struct stun_address *out) {
    switch (out->family) {
    case STUN_FAMILY_IPV4:
        return stun_ipv4_read(r, &out->address.ipv4);
    case STUN_FAMILY_IPV6:
        return stun_ipv6_read(r, &out->address.ipv6);
    default:
        return false; /* unknown tag */
    }
}

static stun_status stun_address_address_write(struct stun_wr *w,
                                              const struct stun_address *in) {
    switch (in->family) {
    case STUN_FAMILY_IPV4:
        return stun_ipv4_write(w, &in->address.ipv4);
    case STUN_FAMILY_IPV6:
        return stun_ipv6_write(w, &in->address.ipv6);
    default:
        return STUN_INVALID; /* unknown tag */
    }
}

static size_t stun_address_address_size(const struct stun_address *in) {
    switch (in->family) {
    case STUN_FAMILY_IPV4:
        return stun_ipv4_size(&in->address.ipv4);
    case STUN_FAMILY_IPV6:
        return stun_ipv6_size(&in->address.ipv6);
    default:
        return 0; /* unknown tag: encode fails */
    }
}

static bool stun_address_read(struct stun_rd *r, struct stun_address *out) {
    STUN_CHECK(stun_rd_skip(r, 1));
    STUN_CHECK(stun_rd_u8(r, &out->family));
    STUN_CHECK(stun_rd_u16(r, &out->port));
    return stun_address_address_read(r, out);
}

static stun_status stun_address_write(struct stun_wr *w,
                                      const struct stun_address *in) {
    STUN_TRY(stun_wr_zero(w, 1));
    STUN_TRY(stun_wr_u8(w, in->family));
    STUN_TRY(stun_wr_u16(w, in->port));
    return stun_address_address_write(w, in);
}

static size_t stun_address_size(const struct stun_address *in) {
    return 4 + stun_address_address_size(in);
}

stun_status stun_address_decode(struct stun_address *out, const uint8_t *buf,
                                size_t len) {
    struct stun_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!stun_address_read(&r, out) || r.p != r.end)
        return STUN_INVALID;
    return STUN_OK;
}

stun_status stun_address_encode(const struct stun_address *in, uint8_t *buf,
                                size_t cap, size_t *written) {
    struct stun_wr w = { buf, cap != 0 ? buf + cap : buf };
    STUN_TRY(stun_address_write(&w, in));
    *written = (size_t)(w.p - buf);
    return STUN_OK;
}

size_t stun_address_encoded_size(const struct stun_address *in) {
    return stun_address_size(in);
}

/* ---- message ErrorCode -------------------------------------------------- */

static bool stun_error_code_read(struct stun_rd *r,
                                 struct stun_error_code *out) {
    uint32_t bits0;
    STUN_CHECK(stun_rd_u24(r, &bits0));
    out->class = (uint8_t)(bits0 & 0x7);
    STUN_CHECK(stun_rd_u8(r, &out->number));
    return stun_rd_rest(r, &out->reason);
}

static stun_status stun_error_code_write(struct stun_wr *w,
                                         const struct stun_error_code *in) {
    if (in->class > 0x7)
        return STUN_INVALID;
    STUN_TRY(stun_wr_u24(w, (uint32_t)(in->class)));
    STUN_TRY(stun_wr_u8(w, in->number));
    return stun_wr_raw(w, in->reason.ptr, in->reason.len);
}

static size_t stun_error_code_size(const struct stun_error_code *in) {
    return 4 + in->reason.len;
}

stun_status stun_error_code_decode(struct stun_error_code *out,
                                   const uint8_t *buf, size_t len) {
    struct stun_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!stun_error_code_read(&r, out) || r.p != r.end)
        return STUN_INVALID;
    return STUN_OK;
}

stun_status stun_error_code_encode(const struct stun_error_code *in,
                                   uint8_t *buf, size_t cap, size_t *written) {
    struct stun_wr w = { buf, cap != 0 ? buf + cap : buf };
    STUN_TRY(stun_error_code_write(&w, in));
    *written = (size_t)(w.p - buf);
    return STUN_OK;
}

size_t stun_error_code_encoded_size(const struct stun_error_code *in) {
    return stun_error_code_size(in);
}

/* ---- message Fingerprint ------------------------------------------------ */

static bool stun_fingerprint_read(struct stun_rd *r,
                                  struct stun_fingerprint *out) {
    return stun_rd_u32(r, &out->crc);
}

static stun_status stun_fingerprint_write(struct stun_wr *w,
                                          const struct stun_fingerprint *in) {
    return stun_wr_u32(w, in->crc);
}

static size_t stun_fingerprint_size(const struct stun_fingerprint *in) {
    (void)in;
    return 4;
}

stun_status stun_fingerprint_decode(struct stun_fingerprint *out,
                                    const uint8_t *buf, size_t len) {
    struct stun_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!stun_fingerprint_read(&r, out) || r.p != r.end)
        return STUN_INVALID;
    return STUN_OK;
}

stun_status stun_fingerprint_encode(const struct stun_fingerprint *in,
                                    uint8_t *buf, size_t cap,
                                    size_t *written) {
    struct stun_wr w = { buf, cap != 0 ? buf + cap : buf };
    STUN_TRY(stun_fingerprint_write(&w, in));
    *written = (size_t)(w.p - buf);
    return STUN_OK;
}

size_t stun_fingerprint_encoded_size(const struct stun_fingerprint *in) {
    return stun_fingerprint_size(in);
}
