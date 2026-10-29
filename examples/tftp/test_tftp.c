/* Round-trip and edge-case tests for the TFTP codec. */
#include <stdio.h>
#include <string.h>

#include "tftp_gen.h"

static int failures;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, \
                    #cond);                                          \
            failures++;                                              \
        }                                                            \
    } while (0)

#define BYTES(...) ((const uint8_t[]){ __VA_ARGS__ }), \
                   sizeof((const uint8_t[]){ __VA_ARGS__ })

/* decode -> encode gives the same bytes, and encoded_size agrees.
 * Every smaller capacity must give NOSPACE. */
static void round_trip(const char *name, const uint8_t *buf, size_t len) {
    struct tftp_packet pkt;
    if (tftp_packet_decode(&pkt, buf, len) != TFTP_OK) {
        fprintf(stderr, "%s: decode failed\n", name);
        failures++;
        return;
    }
    CHECK(tftp_packet_encoded_size(&pkt) == len);
    uint8_t out[1024];
    size_t n = 0;
    CHECK(tftp_packet_encode(&pkt, out, len, &n) == TFTP_OK);
    CHECK(n == len && memcmp(out, buf, len) == 0);
    for (size_t cap = 0; cap < len; cap++)
        CHECK(tftp_packet_encode(&pkt, out, cap, &n) == TFTP_NOSPACE);
}

static void invalid(const char *name, const uint8_t *buf, size_t len) {
    struct tftp_packet pkt;
    if (tftp_packet_decode(&pkt, buf, len) != TFTP_INVALID) {
        fprintf(stderr, "%s: decode should fail\n", name);
        failures++;
    }
}

static void test_valid(void) {
    round_trip("rrq", BYTES(0, 1, 'f', 0, 'o', 'c', 't', 'e', 't', 0));
    round_trip("wrq empty names", BYTES(0, 2, 0, 0));
    round_trip("rrq options",
               BYTES(0, 1, 'f', 0, 'o', 0, 'b', 'l', 'k', 0, '8', 0,
                     't', 0, '1', 0));
    round_trip("data empty", BYTES(0, 3, 0x12, 0x34));
    round_trip("data", BYTES(0, 3, 0xff, 0xfe, 0, 1, 2, 0));
    round_trip("ack", BYTES(0, 4, 0xab, 0xcd));
    round_trip("error", BYTES(0, 5, 0, 1, 'n', 'o', 0));
    round_trip("oack empty", BYTES(0, 6));
    round_trip("oack", BYTES(0, 6, 'a', 0, 'b', 0));

    uint8_t big[2 + 2 + 512];
    big[0] = 0, big[1] = 3, big[2] = 0, big[3] = 1;
    for (size_t i = 4; i < sizeof big; i++)
        big[i] = (uint8_t)i;
    round_trip("data 512", big, sizeof big);
}

static void test_decoded_fields(void) {
    static const uint8_t rrq[] = { 0, 1, 'f', 'i', 'l', 'e', 0,
                                   'o', 'c', 't', 'e', 't', 0,
                                   'b', 'l', 'k', 's', 'i', 'z', 'e', 0,
                                   '1', '4', '2', '8', 0 };
    struct tftp_packet pkt;
    CHECK(tftp_packet_decode(&pkt, rrq, sizeof rrq) == TFTP_OK);
    CHECK(pkt.opcode == TFTP_OPCODE_RRQ);
    CHECK(strcmp(pkt.body.request.filename.ptr, "file") == 0);
    CHECK(pkt.body.request.filename.len == 4);
    CHECK(strcmp(pkt.body.request.mode.ptr, "octet") == 0);
    CHECK(pkt.body.request.options.items == NULL);

    struct tftp_option_iter it;
    struct tftp_option opt;
    tftp_option_iter_init(&it, &pkt.body.request.options);
    CHECK(tftp_option_iter_next(&it, &opt));
    CHECK(strcmp(opt.name.ptr, "blksize") == 0 && opt.name.len == 7);
    CHECK(strcmp(opt.value.ptr, "1428") == 0 && opt.value.len == 4);
    CHECK(!tftp_option_iter_next(&it, &opt));

    static const uint8_t data[] = { 0, 3, 0x01, 0x02, 9, 8 };
    CHECK(tftp_packet_decode(&pkt, data, sizeof data) == TFTP_OK);
    CHECK(pkt.body.data.block == 0x0102);
    CHECK(pkt.body.data.data.ptr == data + 4 && pkt.body.data.data.len == 2);
}

static void test_invalid(void) {
    invalid("null", NULL, 0);
    invalid("one byte", BYTES(0));
    invalid("opcode 0", BYTES(0, 0, 0, 0));
    invalid("opcode 7", BYTES(0, 7, 0, 0));
    invalid("opcode high byte", BYTES(1, 1, 'f', 0, 'o', 0));
    invalid("rrq no fields", BYTES(0, 1));
    invalid("rrq no nul", BYTES(0, 1, 'f', 0, 'o'));
    invalid("rrq no mode", BYTES(0, 1, 'f', 0));
    invalid("rrq option without value", BYTES(0, 1, 'f', 0, 'o', 0, 'a', 0));
    invalid("rrq option value no nul",
            BYTES(0, 1, 'f', 0, 'o', 0, 'a', 0, 'b'));
    invalid("data short", BYTES(0, 3, 0));
    invalid("ack short", BYTES(0, 4, 0));
    invalid("ack trailing", BYTES(0, 4, 0, 1, 0));
    invalid("error no message", BYTES(0, 5, 0, 1));
    invalid("error trailing", BYTES(0, 5, 0, 1, 0, 'x'));
    invalid("oack odd", BYTES(0, 6, 'a', 0));
}

static void test_encode(void) {
    static const struct tftp_option opts[] = {
        { TFTP_CSTR("blksize"), TFTP_CSTR("1428") },
        { TFTP_CSTR("tsize"), TFTP_CSTR("0") },
    };
    struct tftp_packet pkt = {
        .opcode = TFTP_OPCODE_RRQ,
        .body.request = {
            .filename = TFTP_CSTR("boot.img"),
            .mode = TFTP_CSTR("octet"),
            .options = { .items = opts, .count = 2 },
        },
    };
    static const uint8_t want[] = "\0\1boot.img\0octet\0blksize\0001428\0"
                                  "tsize\0" "0";
    uint8_t out[64];
    size_t n = 0;
    CHECK(tftp_packet_encoded_size(&pkt) == sizeof want);
    CHECK(tftp_packet_encode(&pkt, out, sizeof out, &n) == TFTP_OK);
    CHECK(n == sizeof want && memcmp(out, want, n) == 0);

    /* cstr fields need not be NUL-terminated on encode. */
    static const char name[] = { 'a', 'b', 'c' };
    struct tftp_packet err = {
        .opcode = TFTP_OPCODE_ERROR,
        .body.error = { .code = 2, .message = { name, 2 } },
    };
    CHECK(tftp_packet_encode(&err, out, sizeof out, &n) == TFTP_OK);
    CHECK(n == 7 && memcmp(out, "\0\5\0\2ab", 7) == 0);

    /* Embedded NUL in a cstr. */
    err.body.error.message = (struct tftp_cstr){ "a\0b", 3 };
    CHECK(tftp_packet_encode(&err, out, sizeof out, &n) == TFTP_INVALID);

    /* Unknown opcode. */
    struct tftp_packet bad = { .opcode = (enum tftp_opcode)9 };
    CHECK(tftp_packet_encode(&bad, out, sizeof out, &n) == TFTP_INVALID);

    /* Hand-filled raw option bytes are validated. */
    static const uint8_t raw[] = { 'a', 0 };
    struct tftp_packet oack = {
        .opcode = TFTP_OPCODE_OACK,
        .body.oack.options = { .raw = raw, .raw_len = sizeof raw },
    };
    CHECK(tftp_packet_encode(&oack, out, sizeof out, &n) == TFTP_INVALID);

    /* Empty DATA with a NULL view. */
    struct tftp_packet data = { .opcode = TFTP_OPCODE_DATA,
                                .body.data = { .block = 7 } };
    CHECK(tftp_packet_encode(&data, out, sizeof out, &n) == TFTP_OK);
    CHECK(n == 4 && memcmp(out, "\0\3\0\7", 4) == 0);

    /* Zero capacity, NULL buffer. */
    CHECK(tftp_packet_encode(&data, NULL, 0, &n) == TFTP_NOSPACE);
}

int main(void) {
    test_valid();
    test_decoded_fields();
    test_invalid();
    test_encode();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
