/* Generated from tftp.schema -- do not edit. */
#include "tftp_gen.h"

#include <string.h>

/* ---- wire primitives ---------------------------------------------------- */

/* Read cursor over [p, end). Every read checks bounds first. */
struct tftp_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct tftp_wr {
    uint8_t *p;
    uint8_t *end;
};

static size_t tftp_rd_left(const struct tftp_rd *r) {
    return (size_t)(r->end - r->p);
}

static bool tftp_rd_u16(struct tftp_rd *r, uint16_t *v) {
    if (tftp_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}

static bool tftp_rd_cstr(struct tftp_rd *r, struct tftp_cstr *v) {
    if (r->p == r->end)
        return false;
    const uint8_t *nul = memchr(r->p, 0, tftp_rd_left(r));
    if (nul == NULL)
        return false;
    v->ptr = (const char *)r->p;
    v->len = (size_t)(nul - r->p);
    r->p = nul + 1;
    return true;
}

static bool tftp_rd_rest(struct tftp_rd *r, struct tftp_bytes *v) {
    v->ptr = r->p;
    v->len = tftp_rd_left(r);
    r->p = r->end;
    return true;
}

static size_t tftp_wr_left(const struct tftp_wr *w) {
    return (size_t)(w->end - w->p);
}

static tftp_status tftp_wr_u16(struct tftp_wr *w, uint16_t v) {
    if (tftp_wr_left(w) < 2)
        return TFTP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return TFTP_OK;
}

static tftp_status tftp_wr_raw(struct tftp_wr *w, const void *src, size_t n) {
    if (tftp_wr_left(w) < n)
        return TFTP_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return TFTP_OK;
}

static tftp_status tftp_wr_cstr(struct tftp_wr *w, const struct tftp_cstr *v) {
    if (v->len != 0 && memchr(v->ptr, 0, v->len) != NULL)
        return TFTP_INVALID;
    if (tftp_wr_left(w) < v->len + 1)
        return TFTP_NOSPACE;
    tftp_wr_raw(w, v->ptr, v->len);
    w->p[0] = 0;
    w->p += 1;
    return TFTP_OK;
}

static size_t tftp_cstr_size(const struct tftp_cstr *v) {
    return v->len + 1;
}

#define TFTP_CHECK(cond) \
    do { \
        if (!(cond)) \
            return false; \
    } while (0)

#define TFTP_TRY(expr) \
    do { \
        tftp_status st_ = (expr); \
        if (st_ != TFTP_OK) \
            return st_; \
    } while (0)

/* ---- struct Option ------------------------------------------------------ */

static bool tftp_option_read(struct tftp_rd *r, struct tftp_option *out) {
    TFTP_CHECK(tftp_rd_cstr(r, &out->name));
    return tftp_rd_cstr(r, &out->value);
}

static tftp_status tftp_option_write(struct tftp_wr *w,
                                     const struct tftp_option *in) {
    TFTP_TRY(tftp_wr_cstr(w, &in->name));
    return tftp_wr_cstr(w, &in->value);
}

static size_t tftp_option_size(const struct tftp_option *in) {
    return tftp_cstr_size(&in->name) + tftp_cstr_size(&in->value);
}

/* ---- Option[..] --------------------------------------------------------- */

/* Consumes the rest of r. Fails unless it is a whole number of Options. */
static bool tftp_option_list_read(struct tftp_rd *r,
                                  struct tftp_option_list *out) {
    const uint8_t *start = r->p;
    struct tftp_option tmp;
    while (r->p != r->end)
        if (!tftp_option_read(r, &tmp))
            return false;
    out->raw = start;
    out->raw_len = (size_t)(r->p - start);
    out->items = NULL;
    out->count = 0;
    return true;
}

static tftp_status tftp_option_list_write(struct tftp_wr *w,
                                          const struct tftp_option_list *in) {
    if (in->items != NULL) {
        for (size_t i = 0; i < in->count; i++)
            TFTP_TRY(tftp_option_write(w, &in->items[i]));
        return TFTP_OK;
    }
    if (in->raw_len == 0)
        return TFTP_OK;
    /* raw may have been filled in by hand: encode never emits bytes that
     * decode would reject. */
    struct tftp_rd r = { in->raw, in->raw + in->raw_len };
    struct tftp_option_list check;
    if (!tftp_option_list_read(&r, &check))
        return TFTP_INVALID;
    return tftp_wr_raw(w, in->raw, in->raw_len);
}

static size_t tftp_option_list_size(const struct tftp_option_list *in) {
    if (in->items == NULL)
        return in->raw_len;
    size_t n = 0;
    for (size_t i = 0; i < in->count; i++)
        n += tftp_option_size(&in->items[i]);
    return n;
}

void tftp_option_iter_init(struct tftp_option_iter *it,
                           const struct tftp_option_list *list) {
    it->pos = list->raw;
    it->end = list->raw_len != 0 ? list->raw + list->raw_len : list->raw;
}

bool tftp_option_iter_next(struct tftp_option_iter *it,
                           struct tftp_option *out) {
    if (it->pos == it->end)
        return false;
    struct tftp_rd r = { it->pos, it->end };
    if (!tftp_option_read(&r, out))
        return false;
    it->pos = r.p;
    return true;
}

/* ---- struct Request ----------------------------------------------------- */

static bool tftp_request_read(struct tftp_rd *r, struct tftp_request *out) {
    TFTP_CHECK(tftp_rd_cstr(r, &out->filename));
    TFTP_CHECK(tftp_rd_cstr(r, &out->mode));
    return tftp_option_list_read(r, &out->options);
}

static tftp_status tftp_request_write(struct tftp_wr *w,
                                      const struct tftp_request *in) {
    TFTP_TRY(tftp_wr_cstr(w, &in->filename));
    TFTP_TRY(tftp_wr_cstr(w, &in->mode));
    return tftp_option_list_write(w, &in->options);
}

static size_t tftp_request_size(const struct tftp_request *in) {
    return tftp_cstr_size(&in->filename)
         + tftp_cstr_size(&in->mode)
         + tftp_option_list_size(&in->options);
}

/* ---- struct Data -------------------------------------------------------- */

static bool tftp_data_read(struct tftp_rd *r, struct tftp_data *out) {
    TFTP_CHECK(tftp_rd_u16(r, &out->block));
    return tftp_rd_rest(r, &out->data);
}

static tftp_status tftp_data_write(struct tftp_wr *w,
                                   const struct tftp_data *in) {
    TFTP_TRY(tftp_wr_u16(w, in->block));
    return tftp_wr_raw(w, in->data.ptr, in->data.len);
}

static size_t tftp_data_size(const struct tftp_data *in) {
    return 2 + in->data.len;
}

/* ---- struct Ack --------------------------------------------------------- */

static bool tftp_ack_read(struct tftp_rd *r, struct tftp_ack *out) {
    return tftp_rd_u16(r, &out->block);
}

static tftp_status tftp_ack_write(struct tftp_wr *w,
                                  const struct tftp_ack *in) {
    return tftp_wr_u16(w, in->block);
}

static size_t tftp_ack_size(const struct tftp_ack *in) {
    (void)in;
    return 2;
}

/* ---- struct Error ------------------------------------------------------- */

static bool tftp_error_read(struct tftp_rd *r, struct tftp_error *out) {
    TFTP_CHECK(tftp_rd_u16(r, &out->code));
    return tftp_rd_cstr(r, &out->message);
}

static tftp_status tftp_error_write(struct tftp_wr *w,
                                    const struct tftp_error *in) {
    TFTP_TRY(tftp_wr_u16(w, in->code));
    return tftp_wr_cstr(w, &in->message);
}

static size_t tftp_error_size(const struct tftp_error *in) {
    return 2 + tftp_cstr_size(&in->message);
}

/* ---- struct Oack -------------------------------------------------------- */

static bool tftp_oack_read(struct tftp_rd *r, struct tftp_oack *out) {
    return tftp_option_list_read(r, &out->options);
}

static tftp_status tftp_oack_write(struct tftp_wr *w,
                                   const struct tftp_oack *in) {
    return tftp_option_list_write(w, &in->options);
}

static size_t tftp_oack_size(const struct tftp_oack *in) {
    return tftp_option_list_size(&in->options);
}

/* ---- message Packet ----------------------------------------------------- */

static bool tftp_packet_body_read(struct tftp_rd *r, struct tftp_packet *out) {
    switch (out->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        return tftp_request_read(r, &out->body.request);
    case TFTP_OPCODE_DATA:
        return tftp_data_read(r, &out->body.data);
    case TFTP_OPCODE_ACK:
        return tftp_ack_read(r, &out->body.ack);
    case TFTP_OPCODE_ERROR:
        return tftp_error_read(r, &out->body.error);
    case TFTP_OPCODE_OACK:
        return tftp_oack_read(r, &out->body.oack);
    default:
        return false; /* unknown tag */;
    }
}

static tftp_status tftp_packet_body_write(struct tftp_wr *w,
                                          const struct tftp_packet *in) {
    switch (in->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        return tftp_request_write(w, &in->body.request);
    case TFTP_OPCODE_DATA:
        return tftp_data_write(w, &in->body.data);
    case TFTP_OPCODE_ACK:
        return tftp_ack_write(w, &in->body.ack);
    case TFTP_OPCODE_ERROR:
        return tftp_error_write(w, &in->body.error);
    case TFTP_OPCODE_OACK:
        return tftp_oack_write(w, &in->body.oack);
    default:
        return TFTP_INVALID; /* unknown tag */;
    }
}

static size_t tftp_packet_body_size(const struct tftp_packet *in) {
    switch (in->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        return tftp_request_size(&in->body.request);
    case TFTP_OPCODE_DATA:
        return tftp_data_size(&in->body.data);
    case TFTP_OPCODE_ACK:
        return tftp_ack_size(&in->body.ack);
    case TFTP_OPCODE_ERROR:
        return tftp_error_size(&in->body.error);
    case TFTP_OPCODE_OACK:
        return tftp_oack_size(&in->body.oack);
    default:
        return 0; /* unknown tag: encode fails */;
    }
}

static bool tftp_packet_read(struct tftp_rd *r, struct tftp_packet *out) {
    TFTP_CHECK(tftp_rd_u16(r, &out->opcode));
    return tftp_packet_body_read(r, out);
}

static tftp_status tftp_packet_write(struct tftp_wr *w,
                                     const struct tftp_packet *in) {
    TFTP_TRY(tftp_wr_u16(w, in->opcode));
    return tftp_packet_body_write(w, in);
}

static size_t tftp_packet_size(const struct tftp_packet *in) {
    return 2 + tftp_packet_body_size(in);
}

tftp_status tftp_packet_decode(struct tftp_packet *out, const uint8_t *buf,
                               size_t len) {
    struct tftp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!tftp_packet_read(&r, out) || r.p != r.end)
        return TFTP_INVALID;
    return TFTP_OK;
}

tftp_status tftp_packet_encode(const struct tftp_packet *in, uint8_t *buf,
                               size_t cap, size_t *written) {
    struct tftp_wr w = { buf, cap != 0 ? buf + cap : buf };
    TFTP_TRY(tftp_packet_write(&w, in));
    *written = (size_t)(w.p - buf);
    return TFTP_OK;
}

size_t tftp_packet_encoded_size(const struct tftp_packet *in) {
    return tftp_packet_size(in);
}
