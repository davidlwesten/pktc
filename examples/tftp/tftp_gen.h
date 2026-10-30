/* Generated from tftp.pkt -- do not edit. */
#ifndef TFTP_GEN_H
#define TFTP_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TFTP_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    TFTP_INVALID,
    /* encode: output buffer too small */
    TFTP_NOSPACE,
} tftp_status;

/* enum Opcode : u16 */
enum tftp_opcode : uint16_t {
    TFTP_OPCODE_RRQ   = 1,
    TFTP_OPCODE_WRQ   = 2,
    TFTP_OPCODE_DATA  = 3,
    TFTP_OPCODE_ACK   = 4,
    TFTP_OPCODE_ERROR = 5,
    TFTP_OPCODE_OACK  = 6,
};

/* cstr: points into the buffer. After a successful decode, ptr[len] == '\0',
 * so ptr is also usable as a C string. len excludes the terminator. */
struct tftp_cstr {
    const char *ptr;
    size_t len;
};

/* u8[..] and u8[len]: points into the buffer. */
struct tftp_bytes {
    const uint8_t *ptr;
    size_t len;
};

/* struct Option { name: cstr; value: cstr; } */
struct tftp_option {
    struct tftp_cstr name;
    struct tftp_cstr value;
};

/* Option[..]
 * decode: raw/raw_len cover the validated wire bytes; walk them with
 *         tftp_option_iter. items is NULL.
 * encode: if items is non-NULL, the count entries are written;
 *         otherwise raw is copied as-is (so decode -> encode round-trips). */
struct tftp_option_list {
    const uint8_t *raw;
    size_t raw_len;
    const struct tftp_option *items;
    size_t count;
};

struct tftp_option_iter {
    const uint8_t *pos;
    const uint8_t *end;
};

/* struct Request { filename: cstr; mode: cstr; options: Option[..]; } */
struct tftp_request {
    struct tftp_cstr filename;
    struct tftp_cstr mode;
    struct tftp_option_list options;
};

/* struct Data { block: u16; data: u8[..]; } */
struct tftp_data {
    uint16_t block;
    struct tftp_bytes data;
};

/* struct Ack { block: u16; } */
struct tftp_ack {
    uint16_t block;
};

/* struct Error { code: u16; message: cstr; } */
struct tftp_error {
    uint16_t code;
    struct tftp_cstr message;
};

/* struct Oack { options: Option[..]; } */
struct tftp_oack {
    struct tftp_option_list options;
};

/* message Packet { opcode: Opcode; body: switch (opcode) { ... }; } */
struct tftp_packet {
    enum tftp_opcode opcode;  /* selects body */
    union {
        struct tftp_request request;  /* RRQ, WRQ */
        struct tftp_data    data;     /* DATA */
        struct tftp_ack     ack;      /* ACK */
        struct tftp_error   error;    /* ERROR */
        struct tftp_oack    oack;     /* OACK */
    } body;
};

/* Size limits of an encoded Packet, in bytes. */
constexpr size_t TFTP_PACKET_MIN_SIZE = 2;

/* Decode one Packet. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
tftp_status tftp_packet_decode(struct tftp_packet *out, const uint8_t *buf,
                               size_t len);

/* Encode into buf[0..cap). On TFTP_OK, *written is the encoded length. */
tftp_status tftp_packet_encode(const struct tftp_packet *in, uint8_t *buf,
                               size_t cap, size_t *written);

/* Exact number of bytes tftp_packet_encode will write, if it succeeds. */
size_t tftp_packet_encoded_size(const struct tftp_packet *in);

/* Iterate a decoded Option[..]. Decode already validated it,
 * so next() only returns false at the end. */
void tftp_option_iter_init(struct tftp_option_iter *it,
                           const struct tftp_option_list *list);
bool tftp_option_iter_next(struct tftp_option_iter *it,
                           struct tftp_option *out);

/* Initializer for a cstr from a string literal (encode side). */
#define TFTP_CSTR(s) { .ptr = (s), .len = sizeof(s) - 1 }

#endif
