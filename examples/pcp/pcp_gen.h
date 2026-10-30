/* Generated from pcp.pkt -- do not edit. */
#ifndef PCP_GEN_H
#define PCP_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PCP_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    PCP_INVALID,
    /* encode: output buffer too small */
    PCP_NOSPACE,
} pcp_status;

/* Constants from pcp.pkt. */
constexpr uint8_t  PCP_VERSION     = 2;
constexpr uint16_t PCP_SERVER_PORT = 5351;
constexpr uint16_t PCP_CLIENT_PORT = 5350;

/* enum Opcode : u7 */
enum pcp_opcode : uint8_t {
    PCP_OPCODE_ANNOUNCE = 0,
    PCP_OPCODE_MAP      = 1,
    PCP_OPCODE_PEER     = 2,
};

/* enum Result : u8 */
enum pcp_result : uint8_t {
    PCP_RESULT_SUCCESS                 = 0,
    PCP_RESULT_UNSUPP_VERSION          = 1,
    PCP_RESULT_NOT_AUTHORIZED          = 2,
    PCP_RESULT_MALFORMED_REQUEST       = 3,
    PCP_RESULT_UNSUPP_OPCODE           = 4,
    PCP_RESULT_UNSUPP_OPTION           = 5,
    PCP_RESULT_MALFORMED_OPTION        = 6,
    PCP_RESULT_NETWORK_FAILURE         = 7,
    PCP_RESULT_NO_RESOURCES            = 8,
    PCP_RESULT_UNSUPP_PROTOCOL         = 9,
    PCP_RESULT_USER_EX_QUOTA           = 10,
    PCP_RESULT_CANNOT_PROVIDE_EXTERNAL = 11,
    PCP_RESULT_ADDRESS_MISMATCH        = 12,
    PCP_RESULT_EXCESSIVE_REMOTE_PEERS  = 13,
};

/* enum OptionCode : u8 */
enum pcp_option_code : uint8_t {
    PCP_OPTION_CODE_THIRD_PARTY    = 1,
    PCP_OPTION_CODE_PREFER_FAILURE = 2,
    PCP_OPTION_CODE_FILTER         = 3,
};

/* u8[..] and u8[len]: points into the buffer. */
struct pcp_bytes {
    const uint8_t *ptr;
    size_t len;
};

/* struct Option { code: OptionCode; _: u8; len: u16; data: u8[len] pad 4; } */
struct pcp_option {
    enum pcp_option_code code;
    struct pcp_bytes data;
};

/* Option[..]
 * decode: raw/raw_len cover the validated wire bytes; walk them with
 *         pcp_option_iter. items is NULL.
 * encode: if items is non-NULL, the count entries are written;
 *         otherwise raw is copied as-is (so decode -> encode round-trips). */
struct pcp_option_list {
    const uint8_t *raw;
    size_t raw_len;
    const struct pcp_option *items;
    size_t count;
};

struct pcp_option_iter {
    const uint8_t *pos;
    const uint8_t *end;
};

/* struct Announce { options: Option[..]; } */
struct pcp_announce {
    struct pcp_option_list options;
};

/* struct Map {
 *     nonce: u8[12];
 *     protocol: u8;
 *     _: u24;
 *     internal_port: u16;
 *     external_port: u16;
 *     external_ip: u8[16];
 *     options: Option[..];
 * } */
struct pcp_map {
    uint8_t nonce[12];
    uint8_t protocol;
    uint16_t internal_port;
    uint16_t external_port;
    uint8_t external_ip[16];
    struct pcp_option_list options;
};

/* struct Peer {
 *     nonce: u8[12];
 *     protocol: u8;
 *     _: u24;
 *     internal_port: u16;
 *     external_port: u16;
 *     external_ip: u8[16];
 *     remote_port: u16;
 *     _: u16;
 *     remote_ip: u8[16];
 *     options: Option[..];
 * } */
struct pcp_peer {
    uint8_t nonce[12];
    uint8_t protocol;
    uint16_t internal_port;
    uint16_t external_port;
    uint8_t external_ip[16];
    uint16_t remote_port;
    uint8_t remote_ip[16];
    struct pcp_option_list options;
};

/* message Request max 1100 {
 *     version: u8;
 *     r: u1 = 0;
 *     opcode: Opcode;
 *     _: u16;
 *     lifetime: u32;
 *     client_ip: u8[16];
 *     body: switch (opcode) { ... };
 * } */
struct pcp_request {
    uint8_t version;
    enum pcp_opcode opcode;  /* selects body */
    uint32_t lifetime;
    uint8_t client_ip[16];
    union {
        struct pcp_announce announce;  /* ANNOUNCE */
        struct pcp_map      map;       /* MAP */
        struct pcp_peer     peer;      /* PEER */
        struct pcp_bytes    unknown;   /* any other value */
    } body;
};

/* Size limits of an encoded Request, in bytes. Decode and encode
 * reject a Request larger than PCP_REQUEST_MAX_SIZE. */
constexpr size_t PCP_REQUEST_MIN_SIZE = 24;
constexpr size_t PCP_REQUEST_MAX_SIZE = 1100;

/* message Response max 1100 {
 *     version: u8;
 *     r: u1 = 1;
 *     opcode: Opcode;
 *     _: u8;
 *     result: Result;
 *     lifetime: u32;
 *     epoch: u32;
 *     _: u8[12];
 *     body: switch (opcode) { ... };
 * } */
struct pcp_response {
    uint8_t version;
    enum pcp_opcode opcode;  /* selects body */
    enum pcp_result result;
    uint32_t lifetime;
    uint32_t epoch;
    union {
        struct pcp_announce announce;  /* ANNOUNCE */
        struct pcp_map      map;       /* MAP */
        struct pcp_peer     peer;      /* PEER */
        struct pcp_bytes    unknown;   /* any other value */
    } body;
};

/* Size limits of an encoded Response, in bytes. Decode and encode
 * reject a Response larger than PCP_RESPONSE_MAX_SIZE. */
constexpr size_t PCP_RESPONSE_MIN_SIZE = 24;
constexpr size_t PCP_RESPONSE_MAX_SIZE = 1100;

/* Decode one Request. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
pcp_status pcp_request_decode(struct pcp_request *out, const uint8_t *buf,
                              size_t len);

/* Encode into buf[0..cap). On PCP_OK, *written is the encoded length. */
pcp_status pcp_request_encode(const struct pcp_request *in, uint8_t *buf,
                              size_t cap, size_t *written);

/* Exact number of bytes pcp_request_encode will write, if it succeeds. */
size_t pcp_request_encoded_size(const struct pcp_request *in);

/* Decode one Response. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
pcp_status pcp_response_decode(struct pcp_response *out, const uint8_t *buf,
                               size_t len);

/* Encode into buf[0..cap). On PCP_OK, *written is the encoded length. */
pcp_status pcp_response_encode(const struct pcp_response *in, uint8_t *buf,
                               size_t cap, size_t *written);

/* Exact number of bytes pcp_response_encode will write, if it succeeds. */
size_t pcp_response_encoded_size(const struct pcp_response *in);

/* Iterate a decoded Option[..]. Decode already validated it,
 * so next() only returns false at the end. */
void pcp_option_iter_init(struct pcp_option_iter *it,
                          const struct pcp_option_list *list);
bool pcp_option_iter_next(struct pcp_option_iter *it, struct pcp_option *out);

#endif
