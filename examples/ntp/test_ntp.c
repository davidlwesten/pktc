/* Tests for the NTP codec, using packets laid out from RFC 5905 and
 * RFC 9327. */
#include <stdio.h>
#include <string.h>

#include "ntp_gen.h"

static int failures;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, \
                    #cond);                                          \
            failures++;                                              \
        }                                                            \
    } while (0)

static void round_trip(const uint8_t *buf, size_t len) {
    struct ntp_packet p;
    CHECK(ntp_packet_decode(&p, buf, len) == NTP_OK);
    CHECK(ntp_packet_encoded_size(&p) == len);
    uint8_t out[128];
    size_t n = 0;
    CHECK(ntp_packet_encode(&p, out, sizeof out, &n) == NTP_OK);
    CHECK(n == len && memcmp(out, buf, len) == 0);
    for (size_t cap = 0; cap < len; cap++)
        CHECK(ntp_packet_encode(&p, out, cap, &n) == NTP_NOSPACE);
}

/* A server reply (section 7.3): stratum 1 from GPS, followed by a MAC
 * (key id and a 16-byte digest), which the codec leaves to the caller. */
static const uint8_t server_reply[] = {
    0x24, 1, 6, 0xec,                        /* LI 0, VN 4, server; poll 6,
                                                precision -20 */
    0x00, 0x00, 0x00, 0x10,                  /* root delay 16/65536 s */
    0x00, 0x00, 0x00, 0x20,                  /* root dispersion */
    'G', 'P', 'S', 0,                        /* reference id */
    0xe9, 0x5f, 0x2c, 0x00, 0, 0, 0, 0,      /* reference */
    0xe9, 0x5f, 0x2c, 0x10, 0x80, 0, 0, 0,   /* origin */
    0xe9, 0x5f, 0x2c, 0x11, 0x40, 0, 0, 0,   /* receive */
    0xe9, 0x5f, 0x2c, 0x11, 0x40, 0, 0, 1,   /* transmit */
    0, 0, 0, 7,                              /* MAC: key id 7 */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
};

static void test_server_reply(void) {
    struct ntp_packet p;
    CHECK(ntp_packet_decode(&p, server_reply, sizeof server_reply) == NTP_OK);
    CHECK(p.leap == NTP_LEAP_NONE && p.version == NTP_VERSION);
    CHECK(p.mode == NTP_MODE_SERVER);
    const struct ntp_time *t = &p.body.time;
    CHECK(t->stratum == 1 && t->poll == 6 && t->precision == -20);
    CHECK(t->root_delay.seconds == 0 && t->root_delay.fraction == 16);
    CHECK(memcmp(t->reference_id, "GPS", 4) == 0);
    CHECK(t->origin.seconds == 0xe95f2c10 && t->origin.fraction == 1u << 31);
    CHECK(t->transmit.fraction == 0x40000001);
    CHECK(t->extensions.len == 20 && t->extensions.ptr[3] == 7);
    round_trip(server_reply, sizeof server_reply);

    /* The 48-byte header is required. */
    CHECK(ntp_packet_decode(&p, server_reply, 47) == NTP_INVALID);
    CHECK(ntp_packet_decode(&p, server_reply, 48) == NTP_OK);
    CHECK(p.body.time.extensions.len == 0);
}

static void test_client_request(void) {
    /* What an SNTP client sends: everything zero but the first byte and
     * its transmit timestamp. */
    struct ntp_packet req = {
        .leap = NTP_LEAP_UNSYNCHRONIZED,
        .version = NTP_VERSION,
        .mode = NTP_MODE_CLIENT,
        .body.time.transmit = { 0xe95f2c10, 0x80000000 },
    };
    uint8_t out[NTP_PACKET_MIN_SIZE + 47];
    size_t n = 0;
    CHECK(ntp_packet_encoded_size(&req) == 48);
    CHECK(ntp_packet_encode(&req, out, sizeof out, &n) == NTP_OK);
    CHECK(n == 48 && out[0] == 0xe3);  /* LI 3, VN 4, mode 3 */
    CHECK(out[40] == 0xe9 && out[44] == 0x80 && out[47] == 0);
    round_trip(out, n);

    req.version = 8;  /* too wide for u3 */
    CHECK(ntp_packet_encode(&req, out, sizeof out, &n) == NTP_INVALID);
    req.version = NTP_VERSION;
    req.body.time.precision = -128;
    CHECK(ntp_packet_encode(&req, out, sizeof out, &n) == NTP_OK);
    CHECK(out[3] == 0x80);
}

/* RFC 9327 section 2: a READ_VARIABLES response with 5 bytes of data,
 * padded to 8, then no authenticator. */
static const uint8_t control_response[] = {
    0x26, 0x82,                  /* LI 0, VN 4, control; R, opcode 2 */
    0x00, 0x01, 0x06, 0x15,      /* sequence 1, status */
    0x00, 0x00, 0x00, 0x00,      /* association 0, offset 0 */
    0x00, 0x05,                  /* count 5 */
    'a', '=', '1', ',', ' ', 0, 0, 0,
};

static void test_control(void) {
    struct ntp_packet p;
    CHECK(ntp_packet_decode(&p, control_response,
                            sizeof control_response) == NTP_OK);
    CHECK(p.mode == NTP_MODE_CONTROL);
    const struct ntp_control *c = &p.body.control;
    CHECK(c->response == 1 && c->error == 0 && c->more == 0);
    CHECK(c->opcode == NTP_CONTROL_OP_READ_VARIABLES);
    CHECK(c->sequence == 1 && c->status == 0x0615);
    CHECK(c->data.len == 5 && memcmp(c->data.ptr, "a=1, ", 5) == 0);
    CHECK(c->authenticator.len == 0);
    round_trip(control_response, sizeof control_response);

    /* The padding is counted from the data, not from the start of the
     * Control struct (which begins at byte 1 of the packet). */
    uint8_t buf[sizeof control_response + 1];
    memcpy(buf, control_response, sizeof control_response);
    CHECK(ntp_packet_decode(&p, buf, 17) == NTP_INVALID);  /* no padding */
    buf[11] = 4;  /* count 4: no padding at all, the rest is authenticator */
    CHECK(ntp_packet_decode(&p, buf, sizeof control_response) == NTP_OK);
    CHECK(p.body.control.data.len == 4);
    CHECK(p.body.control.authenticator.len == 4);

    /* Padding is ignored on decode and written as zeros. */
    memcpy(buf, control_response, sizeof control_response);
    buf[19] = 0xff;
    CHECK(ntp_packet_decode(&p, buf, sizeof control_response) == NTP_OK);
    uint8_t out[sizeof control_response];
    size_t n = 0;
    CHECK(ntp_packet_encode(&p, out, sizeof out, &n) == NTP_OK);
    CHECK(n == sizeof out && memcmp(out, control_response, n) == 0);

    /* A request: no data at all. */
    struct ntp_packet req = {
        .version = 2,
        .mode = NTP_MODE_CONTROL,
        .body.control = { .opcode = NTP_CONTROL_OP_READ_STATUS,
                          .sequence = 9 },
    };
    CHECK(ntp_packet_encode(&req, out, sizeof out, &n) == NTP_OK);
    static const uint8_t want[] = { 0x16, 0x01, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(n == sizeof want && memcmp(out, want, n) == 0);

    /* Count runs past the end. */
    memcpy(buf, control_response, sizeof control_response);
    buf[11] = 9;
    CHECK(ntp_packet_decode(&p, buf, sizeof control_response) == NTP_INVALID);
}

static void test_other_modes(void) {
    /* Mode 7 (private) and mode 0 (reserved) are kept as bytes. */
    static const uint8_t priv[] = { 0x17, 0x80, 0x03, 0x2a };
    struct ntp_packet p;
    CHECK(ntp_packet_decode(&p, priv, sizeof priv) == NTP_OK);
    CHECK(p.mode == NTP_MODE_PRIVATE && p.version == 2);
    CHECK(p.body.unknown.len == 3 && p.body.unknown.ptr[2] == 0x2a);
    round_trip(priv, sizeof priv);
    CHECK(ntp_packet_decode(&p, priv, 1) == NTP_OK);
    CHECK(ntp_packet_decode(&p, priv, 0) == NTP_INVALID);
}

int main(void) {
    test_server_reply();
    test_client_request();
    test_control();
    test_other_modes();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
