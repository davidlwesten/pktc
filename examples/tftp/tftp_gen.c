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

static void tftp_rd_rest(struct tftp_rd *r, struct tftp_bytes *v) {
    v->ptr = r->p;
    v->len = tftp_rd_left(r);
    r->p = r->end;
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

static tftp_status tftp_wr_raw(struct tftp_wr *w, const void *src, size_t len) {
    if (tftp_wr_left(w) < len)
        return TFTP_NOSPACE;
    if (len != 0)
        memcpy(w->p, src, len);
    w->p += len;
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

#define TFTP_TRY(expr)                    \
    do {                                  \
        tftp_status tftp_st_ = (expr);    \
        if (tftp_st_ != TFTP_OK)          \
            return tftp_st_;              \
    } while (0)

/* ---- struct Option { name: cstr; value: cstr; } ------------------------- */

static bool tftp_option_decode(struct tftp_rd *r, struct tftp_option *out) {
    return tftp_rd_cstr(r, &out->name)
        && tftp_rd_cstr(r, &out->value);
}

static tftp_status tftp_option_encode(struct tftp_wr *w,
                                      const struct tftp_option *in) {
    TFTP_TRY(tftp_wr_cstr(w, &in->name));
    TFTP_TRY(tftp_wr_cstr(w, &in->value));
    return TFTP_OK;
}

static size_t tftp_option_size(const struct tftp_option *in) {
    return tftp_cstr_size(&in->name)
         + tftp_cstr_size(&in->value);
}

/* ---- Option[..] --------------------------------------------------------- */

/* Consumes the rest of r. Fails unless it is a whole number of Options. */
static bool tftp_option_list_decode(struct tftp_rd *r,
                                    struct tftp_option_list *out) {
    const uint8_t *start = r->p;
    struct tftp_option tmp;
    while (r->p != r->end)
        if (!tftp_option_decode(r, &tmp))
            return false;
    out->raw = start;
    out->raw_len = (size_t)(r->p - start);
    out->items = NULL;
    out->count = 0;
    return true;
}

static tftp_status tftp_option_list_encode(struct tftp_wr *w,
                                           const struct tftp_option_list *in) {
    if (in->items != NULL) {
        for (size_t i = 0; i < in->count; i++)
            TFTP_TRY(tftp_option_encode(w, &in->items[i]));
        return TFTP_OK;
    }
    if (in->raw_len == 0)
        return TFTP_OK;
    /* raw may have been filled in by hand: encode never emits bytes that
     * decode would reject. */
    struct tftp_rd r = { in->raw, in->raw + in->raw_len };
    struct tftp_option_list check;
    if (!tftp_option_list_decode(&r, &check))
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
    if (!tftp_option_decode(&r, out))
        return false;
    it->pos = r.p;
    return true;
}

/* ---- struct Request { filename: cstr; mode: cstr; options: Option[..]; } */

static bool tftp_request_decode(struct tftp_rd *r, struct tftp_request *out) {
    return tftp_rd_cstr(r, &out->filename)
        && tftp_rd_cstr(r, &out->mode)
        && tftp_option_list_decode(r, &out->options);
}

static tftp_status tftp_request_encode(struct tftp_wr *w,
                                       const struct tftp_request *in) {
    TFTP_TRY(tftp_wr_cstr(w, &in->filename));
    TFTP_TRY(tftp_wr_cstr(w, &in->mode));
    TFTP_TRY(tftp_option_list_encode(w, &in->options));
    return TFTP_OK;
}

static size_t tftp_request_size(const struct tftp_request *in) {
    return tftp_cstr_size(&in->filename)
         + tftp_cstr_size(&in->mode)
         + tftp_option_list_size(&in->options);
}

/* ---- struct Data { block: u16; data: u8[..]; } -------------------------- */

static bool tftp_data_decode(struct tftp_rd *r, struct tftp_data *out) {
    if (!tftp_rd_u16(r, &out->block))
        return false;
    tftp_rd_rest(r, &out->data);
    return true;
}

static tftp_status tftp_data_encode(struct tftp_wr *w,
                                    const struct tftp_data *in) {
    TFTP_TRY(tftp_wr_u16(w, in->block));
    TFTP_TRY(tftp_wr_raw(w, in->data.ptr, in->data.len));
    return TFTP_OK;
}

static size_t tftp_data_size(const struct tftp_data *in) {
    return 2 + in->data.len;
}

/* ---- struct Ack { block: u16; } ----------------------------------------- */

static bool tftp_ack_decode(struct tftp_rd *r, struct tftp_ack *out) {
    return tftp_rd_u16(r, &out->block);
}

static tftp_status tftp_ack_encode(struct tftp_wr *w,
                                   const struct tftp_ack *in) {
    TFTP_TRY(tftp_wr_u16(w, in->block));
    return TFTP_OK;
}

static size_t tftp_ack_size(const struct tftp_ack *in) {
    (void)in;
    return 2;
}

/* ---- struct Error { code: u16; message: cstr; } ------------------------- */

static bool tftp_error_decode(struct tftp_rd *r, struct tftp_error *out) {
    return tftp_rd_u16(r, &out->code)
        && tftp_rd_cstr(r, &out->message);
}

static tftp_status tftp_error_encode(struct tftp_wr *w,
                                     const struct tftp_error *in) {
    TFTP_TRY(tftp_wr_u16(w, in->code));
    TFTP_TRY(tftp_wr_cstr(w, &in->message));
    return TFTP_OK;
}

static size_t tftp_error_size(const struct tftp_error *in) {
    return 2 + tftp_cstr_size(&in->message);
}

/* ---- struct Oack { options: Option[..]; } ------------------------------- */

static bool tftp_oack_decode(struct tftp_rd *r, struct tftp_oack *out) {
    return tftp_option_list_decode(r, &out->options);
}

static tftp_status tftp_oack_encode(struct tftp_wr *w,
                                    const struct tftp_oack *in) {
    TFTP_TRY(tftp_option_list_encode(w, &in->options));
    return TFTP_OK;
}

static size_t tftp_oack_size(const struct tftp_oack *in) {
    return tftp_option_list_size(&in->options);
}

/* ---- message Packet ----------------------------------------------------- */

tftp_status tftp_packet_decode(struct tftp_packet *out,
                               const uint8_t *buf, size_t len) {
    if (len == 0)
        return TFTP_INVALID;
    struct tftp_rd r = { buf, buf + len };
    uint16_t opcode;
    if (!tftp_rd_u16(&r, &opcode))
        return TFTP_INVALID;
    out->opcode = (enum tftp_opcode)opcode;
    bool ok;
    switch (out->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        ok = tftp_request_decode(&r, &out->body.request);
        break;
    case TFTP_OPCODE_DATA:
        ok = tftp_data_decode(&r, &out->body.data);
        break;
    case TFTP_OPCODE_ACK:
        ok = tftp_ack_decode(&r, &out->body.ack);
        break;
    case TFTP_OPCODE_ERROR:
        ok = tftp_error_decode(&r, &out->body.error);
        break;
    case TFTP_OPCODE_OACK:
        ok = tftp_oack_decode(&r, &out->body.oack);
        break;
    default:
        ok = false; /* closed switch: unknown tag */
        break;
    }
    /* The message must account for every byte of the datagram. */
    if (!ok || r.p != r.end)
        return TFTP_INVALID;
    return TFTP_OK;
}

static tftp_status tftp_packet_encode_body(struct tftp_wr *w,
                                           const struct tftp_packet *in) {
    TFTP_TRY(tftp_wr_u16(w, (uint16_t)in->opcode));
    switch (in->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        return tftp_request_encode(w, &in->body.request);
    case TFTP_OPCODE_DATA:
        return tftp_data_encode(w, &in->body.data);
    case TFTP_OPCODE_ACK:
        return tftp_ack_encode(w, &in->body.ack);
    case TFTP_OPCODE_ERROR:
        return tftp_error_encode(w, &in->body.error);
    case TFTP_OPCODE_OACK:
        return tftp_oack_encode(w, &in->body.oack);
    }
    return TFTP_INVALID; /* closed switch: unknown tag */
}

tftp_status tftp_packet_encode(const struct tftp_packet *in,
                               uint8_t *buf, size_t cap, size_t *written) {
    struct tftp_wr w = { buf, cap != 0 ? buf + cap : buf };
    TFTP_TRY(tftp_packet_encode_body(&w, in));
    *written = (size_t)(w.p - buf);
    return TFTP_OK;
}

size_t tftp_packet_encoded_size(const struct tftp_packet *in) {
    switch (in->opcode) {
    case TFTP_OPCODE_RRQ:
    case TFTP_OPCODE_WRQ:
        return 2 + tftp_request_size(&in->body.request);
    case TFTP_OPCODE_DATA:
        return 2 + tftp_data_size(&in->body.data);
    case TFTP_OPCODE_ACK:
        return 2 + tftp_ack_size(&in->body.ack);
    case TFTP_OPCODE_ERROR:
        return 2 + tftp_error_size(&in->body.error);
    case TFTP_OPCODE_OACK:
        return 2 + tftp_oack_size(&in->body.oack);
    }
    return 0; /* unknown tag: encode fails */
}
