/* Generated from pcp.pkt -- do not edit. */
#include "pcp_gen.h"

#include <string.h>

/* ---- wire primitives ---------------------------------------------------- */

/* Read cursor over [p, end). Every read checks bounds first. */
struct pcp_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct pcp_wr {
    uint8_t *p;
    uint8_t *end;
};

static size_t pcp_rd_left(const struct pcp_rd *r) {
    return (size_t)(r->end - r->p);
}

static bool pcp_rd_u8(struct pcp_rd *r, uint8_t *v) {
    if (pcp_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}

static bool pcp_rd_u16(struct pcp_rd *r, uint16_t *v) {
    if (pcp_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}

static bool pcp_rd_u32(struct pcp_rd *r, uint32_t *v) {
    if (pcp_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}

static bool pcp_rd_skip(struct pcp_rd *r, size_t n) {
    if (pcp_rd_left(r) < n)
        return false;
    r->p += n;
    return true;
}

static bool pcp_rd_raw(struct pcp_rd *r, uint8_t *dst, size_t n) {
    if (pcp_rd_left(r) < n)
        return false;
    memcpy(dst, r->p, n);
    r->p += n;
    return true;
}

static bool pcp_rd_rest(struct pcp_rd *r, struct pcp_bytes *v) {
    v->ptr = r->p;
    v->len = pcp_rd_left(r);
    r->p = r->end;
    return true;
}

static bool pcp_rd_bytes(struct pcp_rd *r, struct pcp_bytes *v, size_t n) {
    if (pcp_rd_left(r) < n)
        return false;
    v->ptr = r->p;
    v->len = n;
    r->p += n;
    return true;
}

static size_t pcp_wr_left(const struct pcp_wr *w) {
    return (size_t)(w->end - w->p);
}

static pcp_status pcp_wr_u8(struct pcp_wr *w, uint8_t v) {
    if (pcp_wr_left(w) < 1)
        return PCP_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return PCP_OK;
}

static pcp_status pcp_wr_u16(struct pcp_wr *w, uint16_t v) {
    if (pcp_wr_left(w) < 2)
        return PCP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return PCP_OK;
}

static pcp_status pcp_wr_u32(struct pcp_wr *w, uint32_t v) {
    if (pcp_wr_left(w) < 4)
        return PCP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 24);
    w->p[1] = (uint8_t)(v >> 16);
    w->p[2] = (uint8_t)(v >> 8);
    w->p[3] = (uint8_t)v;
    w->p += 4;
    return PCP_OK;
}

static pcp_status pcp_wr_raw(struct pcp_wr *w, const void *src, size_t n) {
    if (pcp_wr_left(w) < n)
        return PCP_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return PCP_OK;
}

static pcp_status pcp_wr_zero(struct pcp_wr *w, size_t n) {
    if (pcp_wr_left(w) < n)
        return PCP_NOSPACE;
    if (n != 0)
        memset(w->p, 0, n);
    w->p += n;
    return PCP_OK;
}

/* Zero bytes that pad n bytes to a multiple of align. */
static size_t pcp_pad_len(size_t n, size_t align) {
    return (align - n % align) % align;
}

#define PCP_CHECK(cond) \
    do { \
        if (!(cond)) \
            return false; \
    } while (0)

#define PCP_TRY(expr) \
    do { \
        pcp_status st_ = (expr); \
        if (st_ != PCP_OK) \
            return st_; \
    } while (0)

/* ---- struct Option ------------------------------------------------------ */

static bool pcp_option_read(struct pcp_rd *r, struct pcp_option *out) {
    PCP_CHECK(pcp_rd_u8(r, &out->code));
    PCP_CHECK(pcp_rd_skip(r, 1));
    uint16_t data_len;
    PCP_CHECK(pcp_rd_u16(r, &data_len));
    PCP_CHECK(pcp_rd_bytes(r, &out->data, data_len));
    return pcp_rd_skip(r, pcp_pad_len(data_len, 4));
}

static pcp_status pcp_option_write(struct pcp_wr *w,
                                   const struct pcp_option *in) {
    PCP_TRY(pcp_wr_u8(w, in->code));
    PCP_TRY(pcp_wr_zero(w, 1));
    if (in->data.len > 0xffff)
        return PCP_INVALID;
    PCP_TRY(pcp_wr_u16(w, (uint16_t)(in->data.len)));
    PCP_TRY(pcp_wr_raw(w, in->data.ptr, in->data.len));
    return pcp_wr_zero(w, pcp_pad_len(in->data.len, 4));
}

static size_t pcp_option_size(const struct pcp_option *in) {
    return 4 + in->data.len + pcp_pad_len(in->data.len, 4);
}

/* ---- Option[..] --------------------------------------------------------- */

/* Consumes the rest of r. Fails unless it is a whole number of Options. */
static bool pcp_option_list_read(struct pcp_rd *r,
                                 struct pcp_option_list *out) {
    const uint8_t *start = r->p;
    struct pcp_option tmp;
    while (r->p != r->end)
        if (!pcp_option_read(r, &tmp))
            return false;
    out->raw = start;
    out->raw_len = (size_t)(r->p - start);
    out->items = NULL;
    out->count = 0;
    return true;
}

static pcp_status pcp_option_list_write(struct pcp_wr *w,
                                        const struct pcp_option_list *in) {
    if (in->items != NULL) {
        for (size_t i = 0; i < in->count; i++)
            PCP_TRY(pcp_option_write(w, &in->items[i]));
        return PCP_OK;
    }
    if (in->raw_len == 0)
        return PCP_OK;
    /* raw may have been filled in by hand: encode never emits bytes that
     * decode would reject. */
    struct pcp_rd r = { in->raw, in->raw + in->raw_len };
    struct pcp_option_list check;
    if (!pcp_option_list_read(&r, &check))
        return PCP_INVALID;
    return pcp_wr_raw(w, in->raw, in->raw_len);
}

static size_t pcp_option_list_size(const struct pcp_option_list *in) {
    if (in->items == NULL)
        return in->raw_len;
    size_t n = 0;
    for (size_t i = 0; i < in->count; i++)
        n += pcp_option_size(&in->items[i]);
    return n;
}

void pcp_option_iter_init(struct pcp_option_iter *it,
                          const struct pcp_option_list *list) {
    it->pos = list->raw;
    it->end = list->raw_len != 0 ? list->raw + list->raw_len : list->raw;
}

bool pcp_option_iter_next(struct pcp_option_iter *it, struct pcp_option *out) {
    if (it->pos == it->end)
        return false;
    struct pcp_rd r = { it->pos, it->end };
    if (!pcp_option_read(&r, out))
        return false;
    it->pos = r.p;
    return true;
}

/* ---- struct Announce ---------------------------------------------------- */

static bool pcp_announce_read(struct pcp_rd *r, struct pcp_announce *out) {
    return pcp_option_list_read(r, &out->options);
}

static pcp_status pcp_announce_write(struct pcp_wr *w,
                                     const struct pcp_announce *in) {
    return pcp_option_list_write(w, &in->options);
}

static size_t pcp_announce_size(const struct pcp_announce *in) {
    return pcp_option_list_size(&in->options);
}

/* ---- struct Map --------------------------------------------------------- */

static bool pcp_map_read(struct pcp_rd *r, struct pcp_map *out) {
    PCP_CHECK(pcp_rd_raw(r, out->nonce, 12));
    PCP_CHECK(pcp_rd_u8(r, &out->protocol));
    PCP_CHECK(pcp_rd_skip(r, 3));
    PCP_CHECK(pcp_rd_u16(r, &out->internal_port));
    PCP_CHECK(pcp_rd_u16(r, &out->external_port));
    PCP_CHECK(pcp_rd_raw(r, out->external_ip, 16));
    return pcp_option_list_read(r, &out->options);
}

static pcp_status pcp_map_write(struct pcp_wr *w, const struct pcp_map *in) {
    PCP_TRY(pcp_wr_raw(w, in->nonce, 12));
    PCP_TRY(pcp_wr_u8(w, in->protocol));
    PCP_TRY(pcp_wr_zero(w, 3));
    PCP_TRY(pcp_wr_u16(w, in->internal_port));
    PCP_TRY(pcp_wr_u16(w, in->external_port));
    PCP_TRY(pcp_wr_raw(w, in->external_ip, 16));
    return pcp_option_list_write(w, &in->options);
}

static size_t pcp_map_size(const struct pcp_map *in) {
    return 36 + pcp_option_list_size(&in->options);
}

/* ---- struct Peer -------------------------------------------------------- */

static bool pcp_peer_read(struct pcp_rd *r, struct pcp_peer *out) {
    PCP_CHECK(pcp_rd_raw(r, out->nonce, 12));
    PCP_CHECK(pcp_rd_u8(r, &out->protocol));
    PCP_CHECK(pcp_rd_skip(r, 3));
    PCP_CHECK(pcp_rd_u16(r, &out->internal_port));
    PCP_CHECK(pcp_rd_u16(r, &out->external_port));
    PCP_CHECK(pcp_rd_raw(r, out->external_ip, 16));
    PCP_CHECK(pcp_rd_u16(r, &out->remote_port));
    PCP_CHECK(pcp_rd_skip(r, 2));
    PCP_CHECK(pcp_rd_raw(r, out->remote_ip, 16));
    return pcp_option_list_read(r, &out->options);
}

static pcp_status pcp_peer_write(struct pcp_wr *w, const struct pcp_peer *in) {
    PCP_TRY(pcp_wr_raw(w, in->nonce, 12));
    PCP_TRY(pcp_wr_u8(w, in->protocol));
    PCP_TRY(pcp_wr_zero(w, 3));
    PCP_TRY(pcp_wr_u16(w, in->internal_port));
    PCP_TRY(pcp_wr_u16(w, in->external_port));
    PCP_TRY(pcp_wr_raw(w, in->external_ip, 16));
    PCP_TRY(pcp_wr_u16(w, in->remote_port));
    PCP_TRY(pcp_wr_zero(w, 2));
    PCP_TRY(pcp_wr_raw(w, in->remote_ip, 16));
    return pcp_option_list_write(w, &in->options);
}

static size_t pcp_peer_size(const struct pcp_peer *in) {
    return 56 + pcp_option_list_size(&in->options);
}

/* ---- message Request ---------------------------------------------------- */

static bool pcp_request_body_read(struct pcp_rd *r, struct pcp_request *out) {
    switch (out->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        return pcp_announce_read(r, &out->body.announce);
    case PCP_OPCODE_MAP:
        return pcp_map_read(r, &out->body.map);
    case PCP_OPCODE_PEER:
        return pcp_peer_read(r, &out->body.peer);
    default:
        return pcp_rd_rest(r, &out->body.unknown);
    }
}

static pcp_status pcp_request_body_write(struct pcp_wr *w,
                                         const struct pcp_request *in) {
    switch (in->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        return pcp_announce_write(w, &in->body.announce);
    case PCP_OPCODE_MAP:
        return pcp_map_write(w, &in->body.map);
    case PCP_OPCODE_PEER:
        return pcp_peer_write(w, &in->body.peer);
    default:
        return pcp_wr_raw(w, in->body.unknown.ptr, in->body.unknown.len);
    }
}

static size_t pcp_request_body_size(const struct pcp_request *in) {
    switch (in->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        return pcp_announce_size(&in->body.announce);
    case PCP_OPCODE_MAP:
        return pcp_map_size(&in->body.map);
    case PCP_OPCODE_PEER:
        return pcp_peer_size(&in->body.peer);
    default:
        return in->body.unknown.len;
    }
}

static bool pcp_request_read(struct pcp_rd *r, struct pcp_request *out) {
    PCP_CHECK(pcp_rd_u8(r, &out->version));
    uint8_t bits1;
    PCP_CHECK(pcp_rd_u8(r, &bits1));
    PCP_CHECK((bits1 >> 7) == 0);
    out->opcode = (enum pcp_opcode)(bits1 & 0x7f);
    PCP_CHECK(pcp_rd_skip(r, 2));
    PCP_CHECK(pcp_rd_u32(r, &out->lifetime));
    PCP_CHECK(pcp_rd_raw(r, out->client_ip, 16));
    return pcp_request_body_read(r, out);
}

static pcp_status pcp_request_write(struct pcp_wr *w,
                                    const struct pcp_request *in) {
    PCP_TRY(pcp_wr_u8(w, in->version));
    if (in->opcode > 0x7f)
        return PCP_INVALID;
    PCP_TRY(pcp_wr_u8(w, (uint8_t)(in->opcode)));
    PCP_TRY(pcp_wr_zero(w, 2));
    PCP_TRY(pcp_wr_u32(w, in->lifetime));
    PCP_TRY(pcp_wr_raw(w, in->client_ip, 16));
    return pcp_request_body_write(w, in);
}

static size_t pcp_request_size(const struct pcp_request *in) {
    return 24 + pcp_request_body_size(in);
}

pcp_status pcp_request_decode(struct pcp_request *out, const uint8_t *buf,
                              size_t len) {
    if (len > PCP_REQUEST_MAX_SIZE)
        return PCP_INVALID;
    struct pcp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!pcp_request_read(&r, out) || r.p != r.end)
        return PCP_INVALID;
    return PCP_OK;
}

pcp_status pcp_request_encode(const struct pcp_request *in, uint8_t *buf,
                              size_t cap, size_t *written) {
    if (pcp_request_size(in) > PCP_REQUEST_MAX_SIZE)
        return PCP_INVALID;
    struct pcp_wr w = { buf, cap != 0 ? buf + cap : buf };
    PCP_TRY(pcp_request_write(&w, in));
    *written = (size_t)(w.p - buf);
    return PCP_OK;
}

size_t pcp_request_encoded_size(const struct pcp_request *in) {
    return pcp_request_size(in);
}

/* ---- message Response --------------------------------------------------- */

static bool pcp_response_body_read(struct pcp_rd *r,
                                   struct pcp_response *out) {
    switch (out->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        return pcp_announce_read(r, &out->body.announce);
    case PCP_OPCODE_MAP:
        return pcp_map_read(r, &out->body.map);
    case PCP_OPCODE_PEER:
        return pcp_peer_read(r, &out->body.peer);
    default:
        return pcp_rd_rest(r, &out->body.unknown);
    }
}

static pcp_status pcp_response_body_write(struct pcp_wr *w,
                                          const struct pcp_response *in) {
    switch (in->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        return pcp_announce_write(w, &in->body.announce);
    case PCP_OPCODE_MAP:
        return pcp_map_write(w, &in->body.map);
    case PCP_OPCODE_PEER:
        return pcp_peer_write(w, &in->body.peer);
    default:
        return pcp_wr_raw(w, in->body.unknown.ptr, in->body.unknown.len);
    }
}

static size_t pcp_response_body_size(const struct pcp_response *in) {
    switch (in->opcode) {
    case PCP_OPCODE_ANNOUNCE:
        return pcp_announce_size(&in->body.announce);
    case PCP_OPCODE_MAP:
        return pcp_map_size(&in->body.map);
    case PCP_OPCODE_PEER:
        return pcp_peer_size(&in->body.peer);
    default:
        return in->body.unknown.len;
    }
}

static bool pcp_response_read(struct pcp_rd *r, struct pcp_response *out) {
    PCP_CHECK(pcp_rd_u8(r, &out->version));
    uint8_t bits1;
    PCP_CHECK(pcp_rd_u8(r, &bits1));
    PCP_CHECK((bits1 >> 7) == 1);
    out->opcode = (enum pcp_opcode)(bits1 & 0x7f);
    PCP_CHECK(pcp_rd_skip(r, 1));
    PCP_CHECK(pcp_rd_u8(r, &out->result));
    PCP_CHECK(pcp_rd_u32(r, &out->lifetime));
    PCP_CHECK(pcp_rd_u32(r, &out->epoch));
    PCP_CHECK(pcp_rd_skip(r, 12));
    return pcp_response_body_read(r, out);
}

static pcp_status pcp_response_write(struct pcp_wr *w,
                                     const struct pcp_response *in) {
    PCP_TRY(pcp_wr_u8(w, in->version));
    if (in->opcode > 0x7f)
        return PCP_INVALID;
    PCP_TRY(pcp_wr_u8(w, (uint8_t)(0x80 | in->opcode)));
    PCP_TRY(pcp_wr_zero(w, 1));
    PCP_TRY(pcp_wr_u8(w, in->result));
    PCP_TRY(pcp_wr_u32(w, in->lifetime));
    PCP_TRY(pcp_wr_u32(w, in->epoch));
    PCP_TRY(pcp_wr_zero(w, 12));
    return pcp_response_body_write(w, in);
}

static size_t pcp_response_size(const struct pcp_response *in) {
    return 24 + pcp_response_body_size(in);
}

pcp_status pcp_response_decode(struct pcp_response *out, const uint8_t *buf,
                               size_t len) {
    if (len > PCP_RESPONSE_MAX_SIZE)
        return PCP_INVALID;
    struct pcp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!pcp_response_read(&r, out) || r.p != r.end)
        return PCP_INVALID;
    return PCP_OK;
}

pcp_status pcp_response_encode(const struct pcp_response *in, uint8_t *buf,
                               size_t cap, size_t *written) {
    if (pcp_response_size(in) > PCP_RESPONSE_MAX_SIZE)
        return PCP_INVALID;
    struct pcp_wr w = { buf, cap != 0 ? buf + cap : buf };
    PCP_TRY(pcp_response_write(&w, in));
    *written = (size_t)(w.p - buf);
    return PCP_OK;
}

size_t pcp_response_encoded_size(const struct pcp_response *in) {
    return pcp_response_size(in);
}
