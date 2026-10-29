/* Generated from ntp.schema -- do not edit. */
#ifndef NTP_GEN_H
#define NTP_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NTP_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    NTP_INVALID,
    /* encode: output buffer too small */
    NTP_NOSPACE,
} ntp_status;

/* Constants from ntp.schema. */
#define NTP_PORT    123
#define NTP_VERSION 4

/* enum Leap : u2 */
enum ntp_leap : uint8_t {
    NTP_LEAP_NONE           = 0,
    NTP_LEAP_ADD_SECOND     = 1,
    NTP_LEAP_DEL_SECOND     = 2,
    NTP_LEAP_UNSYNCHRONIZED = 3,
};

/* enum Mode : u3 */
enum ntp_mode : uint8_t {
    NTP_MODE_RESERVED          = 0,
    NTP_MODE_SYMMETRIC_ACTIVE  = 1,
    NTP_MODE_SYMMETRIC_PASSIVE = 2,
    NTP_MODE_CLIENT            = 3,
    NTP_MODE_SERVER            = 4,
    NTP_MODE_BROADCAST         = 5,
    NTP_MODE_CONTROL           = 6,
    NTP_MODE_PRIVATE           = 7,
};

/* enum ControlOp : u5 */
enum ntp_control_op : uint8_t {
    NTP_CONTROL_OP_READ_STATUS           = 1,
    NTP_CONTROL_OP_READ_VARIABLES        = 2,
    NTP_CONTROL_OP_WRITE_VARIABLES       = 3,
    NTP_CONTROL_OP_READ_CLOCK_VARIABLES  = 4,
    NTP_CONTROL_OP_WRITE_CLOCK_VARIABLES = 5,
    NTP_CONTROL_OP_SET_TRAP              = 6,
    NTP_CONTROL_OP_TRAP_RESPONSE         = 7,
};

/* u8[..] and u8[len]: points into the buffer. */
struct ntp_bytes {
    const uint8_t *ptr;
    size_t len;
};

/* struct Short { seconds: u16; fraction: u16; } */
struct ntp_short {
    uint16_t seconds;
    uint16_t fraction;
};

/* struct Timestamp { seconds: u32; fraction: u32; } */
struct ntp_timestamp {
    uint32_t seconds;
    uint32_t fraction;
};

/* struct Time {
 *     stratum: u8;
 *     poll: i8;
 *     precision: i8;
 *     root_delay: Short;
 *     root_dispersion: Short;
 *     reference_id: u8[4];
 *     reference: Timestamp;
 *     origin: Timestamp;
 *     receive: Timestamp;
 *     transmit: Timestamp;
 *     extensions: u8[..];
 * } */
struct ntp_time {
    uint8_t stratum;
    int8_t poll;
    int8_t precision;
    struct ntp_short root_delay;
    struct ntp_short root_dispersion;
    uint8_t reference_id[4];
    struct ntp_timestamp reference;
    struct ntp_timestamp origin;
    struct ntp_timestamp receive;
    struct ntp_timestamp transmit;
    struct ntp_bytes extensions;
};

/* struct Control {
 *     response: u1;
 *     error: u1;
 *     more: u1;
 *     opcode: ControlOp;
 *     sequence: u16;
 *     status: u16;
 *     association_id: u16;
 *     offset: u16;
 *     count: u16;
 *     data: u8[count] pad 4;
 *     authenticator: u8[..];
 * } */
struct ntp_control {
    uint8_t response;
    uint8_t error;
    uint8_t more;
    enum ntp_control_op opcode;
    uint16_t sequence;
    uint16_t status;
    uint16_t association_id;
    uint16_t offset;
    struct ntp_bytes data;
    struct ntp_bytes authenticator;
};

/* message Packet {
 *     leap: Leap;
 *     version: u3;
 *     mode: Mode;
 *     body: switch (mode) { ... };
 * } */
struct ntp_packet {
    enum ntp_leap leap;
    uint8_t version;
    enum ntp_mode mode;  /* selects body */
    union {
        struct ntp_time    time;     /* SYMMETRIC_ACTIVE, SYMMETRIC_PASSIVE,
                                        CLIENT, SERVER, BROADCAST */
        struct ntp_control control;  /* CONTROL */
        struct ntp_bytes   unknown;  /* any other value */
    } body;
};

/* Size limits of an encoded Packet, in bytes. */
#define NTP_PACKET_MIN_SIZE 1

/* Decode one Packet. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
ntp_status ntp_packet_decode(struct ntp_packet *out, const uint8_t *buf,
                             size_t len);

/* Encode into buf[0..cap). On NTP_OK, *written is the encoded length. */
ntp_status ntp_packet_encode(const struct ntp_packet *in, uint8_t *buf,
                             size_t cap, size_t *written);

/* Exact number of bytes ntp_packet_encode will write, if it succeeds. */
size_t ntp_packet_encoded_size(const struct ntp_packet *in);

#endif
