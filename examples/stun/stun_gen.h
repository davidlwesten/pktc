/* Generated from stun.schema -- do not edit. */
#ifndef STUN_GEN_H
#define STUN_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    STUN_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    STUN_INVALID,
    /* encode: output buffer too small */
    STUN_NOSPACE,
} stun_status;

/* Constants from stun.schema. */
constexpr uint16_t STUN_PORT         = 3478;
constexpr uint16_t STUN_TLS_PORT     = 5349;
constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

/* enum Type : u14 */
enum stun_type : uint16_t {
    STUN_TYPE_BINDING_REQUEST    = 1,
    STUN_TYPE_BINDING_INDICATION = 17,
    STUN_TYPE_BINDING_SUCCESS    = 257,
    STUN_TYPE_BINDING_ERROR      = 273,
};

/* enum AttributeType : u16 */
enum stun_attribute_type : uint16_t {
    STUN_ATTRIBUTE_TYPE_MAPPED_ADDRESS           = 1,
    STUN_ATTRIBUTE_TYPE_USERNAME                 = 6,
    STUN_ATTRIBUTE_TYPE_MESSAGE_INTEGRITY        = 8,
    STUN_ATTRIBUTE_TYPE_ERROR_CODE               = 9,
    STUN_ATTRIBUTE_TYPE_UNKNOWN_ATTRIBUTES       = 10,
    STUN_ATTRIBUTE_TYPE_REALM                    = 20,
    STUN_ATTRIBUTE_TYPE_NONCE                    = 21,
    STUN_ATTRIBUTE_TYPE_MESSAGE_INTEGRITY_SHA256 = 28,
    STUN_ATTRIBUTE_TYPE_PASSWORD_ALGORITHM       = 29,
    STUN_ATTRIBUTE_TYPE_USERHASH                 = 30,
    STUN_ATTRIBUTE_TYPE_XOR_MAPPED_ADDRESS       = 32,
    STUN_ATTRIBUTE_TYPE_PASSWORD_ALGORITHMS      = 32770,
    STUN_ATTRIBUTE_TYPE_ALTERNATE_DOMAIN         = 32771,
    STUN_ATTRIBUTE_TYPE_SOFTWARE                 = 32802,
    STUN_ATTRIBUTE_TYPE_ALTERNATE_SERVER         = 32803,
    STUN_ATTRIBUTE_TYPE_FINGERPRINT              = 32808,
};

/* enum Family : u8 */
enum stun_family : uint8_t {
    STUN_FAMILY_IPV4 = 1,
    STUN_FAMILY_IPV6 = 2,
};

/* u8[..] and u8[len]: points into the buffer. */
struct stun_bytes {
    const uint8_t *ptr;
    size_t len;
};

/* struct Attribute {
 *     type: AttributeType;
 *     length: u16;
 *     value: u8[length] pad 4;
 * } */
struct stun_attribute {
    enum stun_attribute_type type;
    struct stun_bytes value;
};

/* Attribute[..]
 * decode: raw/raw_len cover the validated wire bytes; walk them with
 *         stun_attribute_iter. items is NULL.
 * encode: if items is non-NULL, the count entries are written;
 *         otherwise raw is copied as-is (so decode -> encode round-trips). */
struct stun_attribute_list {
    const uint8_t *raw;
    size_t raw_len;
    const struct stun_attribute *items;
    size_t count;
};

struct stun_attribute_iter {
    const uint8_t *pos;
    const uint8_t *end;
};

/* message Message {
 *     zero: u2 = 0;
 *     type: Type;
 *     length: u16;
 *     cookie: u32 = 0x2112a442;
 *     transaction_id: u8[12];
 *     attributes: Attribute[length];
 * } */
struct stun_message {
    enum stun_type type;
    uint8_t transaction_id[12];
    struct stun_attribute_list attributes;
};

/* Size limits of an encoded Message, in bytes. */
constexpr size_t STUN_MESSAGE_MIN_SIZE = 20;
constexpr size_t STUN_MESSAGE_MAX_SIZE = 65555;

/* struct Ipv4 { addr: u8[4]; } */
struct stun_ipv4 {
    uint8_t addr[4];
};

/* struct Ipv6 { addr: u8[16]; } */
struct stun_ipv6 {
    uint8_t addr[16];
};

/* message Address {
 *     _: u8;
 *     family: Family;
 *     port: u16;
 *     address: switch (family) { ... };
 * } */
struct stun_address {
    enum stun_family family;  /* selects address */
    uint16_t port;
    union {
        struct stun_ipv4 ipv4;  /* IPV4 */
        struct stun_ipv6 ipv6;  /* IPV6 */
    } address;
};

/* Size limits of an encoded Address, in bytes. */
constexpr size_t STUN_ADDRESS_MIN_SIZE = 8;
constexpr size_t STUN_ADDRESS_MAX_SIZE = 20;

/* message ErrorCode { _: u21; class: u3; number: u8; reason: u8[..]; } */
struct stun_error_code {
    uint8_t class;
    uint8_t number;
    struct stun_bytes reason;
};

/* Size limits of an encoded ErrorCode, in bytes. */
constexpr size_t STUN_ERROR_CODE_MIN_SIZE = 4;

/* message Fingerprint { crc: u32; } */
struct stun_fingerprint {
    uint32_t crc;
};

/* Size limits of an encoded Fingerprint, in bytes. */
constexpr size_t STUN_FINGERPRINT_MIN_SIZE = 4;
constexpr size_t STUN_FINGERPRINT_MAX_SIZE = 4;

/* Decode one Message. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
stun_status stun_message_decode(struct stun_message *out, const uint8_t *buf,
                                size_t len);

/* Encode into buf[0..cap). On STUN_OK, *written is the encoded length. */
stun_status stun_message_encode(const struct stun_message *in, uint8_t *buf,
                                size_t cap, size_t *written);

/* Exact number of bytes stun_message_encode will write, if it succeeds. */
size_t stun_message_encoded_size(const struct stun_message *in);

/* Decode one Address. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
stun_status stun_address_decode(struct stun_address *out, const uint8_t *buf,
                                size_t len);

/* Encode into buf[0..cap). On STUN_OK, *written is the encoded length. */
stun_status stun_address_encode(const struct stun_address *in, uint8_t *buf,
                                size_t cap, size_t *written);

/* Exact number of bytes stun_address_encode will write, if it succeeds. */
size_t stun_address_encoded_size(const struct stun_address *in);

/* Decode one ErrorCode. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
stun_status stun_error_code_decode(struct stun_error_code *out,
                                   const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On STUN_OK, *written is the encoded length. */
stun_status stun_error_code_encode(const struct stun_error_code *in,
                                   uint8_t *buf, size_t cap, size_t *written);

/* Exact number of bytes stun_error_code_encode will write, if it succeeds. */
size_t stun_error_code_encoded_size(const struct stun_error_code *in);

/* Decode one Fingerprint. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
stun_status stun_fingerprint_decode(struct stun_fingerprint *out,
                                    const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On STUN_OK, *written is the encoded length. */
stun_status stun_fingerprint_encode(const struct stun_fingerprint *in,
                                    uint8_t *buf, size_t cap, size_t *written);

/* Exact number of bytes stun_fingerprint_encode will write, if it succeeds. */
size_t stun_fingerprint_encoded_size(const struct stun_fingerprint *in);

/* Iterate a decoded Attribute[..]. Decode already validated it,
 * so next() only returns false at the end. */
void stun_attribute_iter_init(struct stun_attribute_iter *it,
                              const struct stun_attribute_list *list);
bool stun_attribute_iter_next(struct stun_attribute_iter *it,
                              struct stun_attribute *out);

#endif
