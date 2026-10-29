/* Tests for the PCP codec, using packets laid out from RFC 6887. */
#include <stdio.h>
#include <string.h>

#include "pcp_gen.h"

static int failures;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, \
                    #cond);                                          \
            failures++;                                              \
        }                                                            \
    } while (0)

/* MAP request (section 11.1) with a PREFER_FAILURE and a THIRD_PARTY option. */
static const uint8_t map_request[] = {
    2, 0x01, 0, 0,                                   /* version, R=0 opcode=MAP */
    0x00, 0x00, 0x1c, 0x20,                          /* lifetime 7200 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff,        /* client ::ffff:192.168.1.10 */
    192, 168, 1, 10,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,           /* nonce */
    6, 0, 0, 0,                                      /* protocol TCP, reserved */
    0x1f, 0x90, 0x00, 0x00,                          /* internal 8080, external 0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* suggested external :: */
    2, 0, 0, 0,                                      /* PREFER_FAILURE, len 0 */
    1, 0, 0, 16,                                     /* THIRD_PARTY, len 16 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, 0, 7,
};

static void round_trip_request(const uint8_t *buf, size_t len) {
    struct pcp_request req;
    CHECK(pcp_request_decode(&req, buf, len) == PCP_OK);
    CHECK(pcp_request_encoded_size(&req) == len);
    uint8_t out[PCP_REQUEST_MAX_SIZE];
    size_t n = 0;
    CHECK(pcp_request_encode(&req, out, sizeof out, &n) == PCP_OK);
    CHECK(n == len && memcmp(out, buf, len) == 0);
    for (size_t cap = 0; cap < len; cap++)
        CHECK(pcp_request_encode(&req, out, cap, &n) == PCP_NOSPACE);
}

static void test_map_request(void) {
    struct pcp_request req;
    CHECK(pcp_request_decode(&req, map_request, sizeof map_request) == PCP_OK);
    CHECK(req.version == PCP_VERSION);
    CHECK(req.opcode == PCP_OPCODE_MAP);
    CHECK(req.lifetime == 7200);
    CHECK(req.client_ip[12] == 192 && req.client_ip[15] == 10);
    const struct pcp_map *map = &req.body.map;
    CHECK(map->nonce[0] == 1 && map->nonce[11] == 12);
    CHECK(map->protocol == 6 && map->internal_port == 8080);
    CHECK(map->external_port == 0);

    struct pcp_option_iter it;
    struct pcp_option opt;
    pcp_option_iter_init(&it, &map->options);
    CHECK(pcp_option_iter_next(&it, &opt));
    CHECK(opt.code == PCP_OPTION_CODE_PREFER_FAILURE && opt.data.len == 0);
    CHECK(pcp_option_iter_next(&it, &opt));
    CHECK(opt.code == PCP_OPTION_CODE_THIRD_PARTY && opt.data.len == 16);
    CHECK(opt.data.ptr[15] == 7);
    CHECK(!pcp_option_iter_next(&it, &opt));

    round_trip_request(map_request, sizeof map_request);
}

static void test_header_fields(void) {
    uint8_t buf[sizeof map_request];
    struct pcp_request req;
    struct pcp_response resp;

    /* R=1 is a response: a request decoder must reject it. */
    memcpy(buf, map_request, sizeof buf);
    buf[1] = 0x81;
    CHECK(pcp_request_decode(&req, buf, sizeof buf) == PCP_INVALID);
    CHECK(pcp_response_decode(&resp, map_request, sizeof map_request) == PCP_INVALID);

    /* Version is the caller's to check (so it can answer UNSUPP_VERSION). */
    memcpy(buf, map_request, sizeof buf);
    buf[0] = 3;
    CHECK(pcp_request_decode(&req, buf, sizeof buf) == PCP_OK && req.version == 3);

    /* Reserved bits are ignored on decode and written as zero on encode. */
    memcpy(buf, map_request, sizeof buf);
    buf[2] = 0xff;
    buf[37] = 0xaa;  /* Map's 24 reserved bits */
    CHECK(pcp_request_decode(&req, buf, sizeof buf) == PCP_OK);
    uint8_t out[sizeof buf];
    size_t n = 0;
    CHECK(pcp_request_encode(&req, out, sizeof out, &n) == PCP_OK);
    CHECK(n == sizeof buf && memcmp(out, map_request, n) == 0);
}

static void test_unknown_opcode(void) {
    /* Opcode 5 is unsupported: the body is kept as bytes for UNSUPP_OPCODE. */
    uint8_t buf[32] = { 2, 0x05 };
    struct pcp_request req;
    CHECK(pcp_request_decode(&req, buf, sizeof buf) == PCP_OK);
    CHECK(req.opcode == 5 && req.body.unknown.len == 8);
    round_trip_request(buf, sizeof buf);

    /* Too short for the 24-byte header. */
    CHECK(pcp_request_decode(&req, buf, 23) == PCP_INVALID);
}

static void test_max_size(void) {
    /* ANNOUNCE with one option: 24 + 4 + data + padding. */
    static uint8_t buf[1200];
    struct pcp_request req;
    memset(buf, 0, sizeof buf);
    buf[0] = 2;
    buf[24] = 1;
    buf[26] = 1072 >> 8, buf[27] = 1072 & 0xff;
    CHECK(pcp_request_decode(&req, buf, 1100) == PCP_OK);
    round_trip_request(buf, 1100);

    buf[26] = 1076 >> 8, buf[27] = 1076 & 0xff;
    CHECK(pcp_request_decode(&req, buf, 1104) == PCP_INVALID);

    /* Encode refuses to exceed the maximum as well. */
    static const uint8_t big[1100] = { 0 };
    struct pcp_option opt = { .code = PCP_OPTION_CODE_FILTER,
                              .data = { big, sizeof big } };
    struct pcp_request a = { .version = 2, .opcode = PCP_OPCODE_ANNOUNCE,
                             .body.announce.options = { .items = &opt, .count = 1 } };
    size_t n = 0;
    CHECK(pcp_request_encode(&a, buf, sizeof buf, &n) == PCP_INVALID);
}

static void test_options(void) {
    struct pcp_request req;
    /* Option length runs past the end. */
    uint8_t trunc[32] = { 2, 0x00 };
    trunc[24] = 1, trunc[27] = 16;
    CHECK(pcp_request_decode(&req, trunc, sizeof trunc) == PCP_INVALID);

    /* 1 byte of data, but the 3 padding bytes are missing. */
    uint8_t nopad[29] = { 2, 0x00 };
    nopad[24] = 1, nopad[27] = 1;
    CHECK(pcp_request_decode(&req, nopad, sizeof nopad) == PCP_INVALID);
    uint8_t padded[32] = { 2, 0x00 };
    padded[24] = 1, padded[27] = 1;
    CHECK(pcp_request_decode(&req, padded, sizeof padded) == PCP_OK);
}

static void test_encode_response(void) {
    static const uint8_t ip[16] = { [10] = 0xff, [11] = 0xff, 203, 0, 113, 5 };
    static const uint8_t odd[3] = { 0xaa, 0xbb, 0xcc };
    const struct pcp_option opts[] = {
        { .code = PCP_OPTION_CODE_FILTER, .data = { odd, sizeof odd } },
    };
    struct pcp_response resp = {
        .version = PCP_VERSION,
        .opcode = PCP_OPCODE_MAP,
        .result = PCP_RESULT_SUCCESS,
        .lifetime = 3600,
        .epoch = 0x01020304,
        .body.map = {
            .nonce = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 },
            .protocol = 17,
            .internal_port = 53,
            .external_port = 40053,
        },
    };
    memcpy(resp.body.map.external_ip, ip, sizeof ip);
    resp.body.map.options = (struct pcp_option_list){ .items = opts, .count = 1 };

    uint8_t out[128];
    size_t n = 0;
    CHECK(pcp_response_encoded_size(&resp) == 24 + 36 + 8);
    CHECK(pcp_response_encode(&resp, out, sizeof out, &n) == PCP_OK);
    CHECK(n == 68);
    CHECK(out[0] == 2 && out[1] == 0x81 && out[3] == 0);     /* R=1 | MAP */
    CHECK(out[8] == 1 && out[11] == 4);                      /* epoch */
    CHECK(out[58] == 113 && out[59] == 5);                   /* external ip */
    static const uint8_t opt_bytes[] = { 3, 0, 0, 3, 0xaa, 0xbb, 0xcc, 0 };
    CHECK(memcmp(out + 60, opt_bytes, sizeof opt_bytes) == 0);

    struct pcp_response back;
    CHECK(pcp_response_decode(&back, out, n) == PCP_OK);
    CHECK(back.body.map.external_port == 40053 && back.epoch == 0x01020304);

    /* An opcode too wide for its 7 bits. */
    resp.opcode = (enum pcp_opcode)0x80;
    CHECK(pcp_response_encode(&resp, out, sizeof out, &n) == PCP_INVALID);
}

int main(void) {
    test_map_request();
    test_header_fields();
    test_unknown_opcode();
    test_max_size();
    test_options();
    test_encode_response();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
