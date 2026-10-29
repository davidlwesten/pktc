#include <stdio.h>
#include "tftp_gen.h"

void on_datagram(const uint8_t *buf, size_t len) {
    struct tftp_packet pkt;
    if (tftp_packet_decode(&pkt, buf, len) != TFTP_OK)
        return;
    switch (pkt.opcode) {
    case TFTP_OPCODE_RRQ: {
        printf("read %s (%s)\n", pkt.body.request.filename.ptr, pkt.body.request.mode.ptr);
        struct tftp_option_iter it;
        struct tftp_option opt;
        tftp_option_iter_init(&it, &pkt.body.request.options);
        while (tftp_option_iter_next(&it, &opt))
            printf("  %s=%s\n", opt.name.ptr, opt.value.ptr);
        break;
    }
    case TFTP_OPCODE_DATA:
        printf("block %u: %zu bytes\n", pkt.body.data.block, pkt.body.data.data.len);
        break;
    default:
        break;
    }
}

size_t make_rrq(uint8_t *buf, size_t cap) {
    static const struct tftp_option opts[] = {
        { TFTP_CSTR("blksize"), TFTP_CSTR("1428") },
    };
    struct tftp_packet pkt = {
        .opcode = TFTP_OPCODE_RRQ,
        .body.request = {
            .filename = TFTP_CSTR("boot.img"),
            .mode     = TFTP_CSTR("octet"),
            .options  = { .items = opts, .count = 1 },
        },
    };
    size_t n = 0;
    return tftp_packet_encode(&pkt, buf, cap, &n) == TFTP_OK ? n : 0;
}
