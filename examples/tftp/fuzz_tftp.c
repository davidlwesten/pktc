/* libFuzzer harness: decode never reads out of bounds, and anything that
 * decodes re-encodes to identical bytes.
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       fuzz_tftp.c tftp_gen.c -o fuzz_tftp
 */
#include <stdlib.h>
#include <string.h>

#include "tftp_gen.h"

static void walk_options(const struct tftp_option_list *list) {
    struct tftp_option_iter it;
    struct tftp_option opt;
    tftp_option_iter_init(&it, list);
    while (tftp_option_iter_next(&it, &opt))
        if (opt.name.ptr[opt.name.len] != '\0' ||
            opt.value.ptr[opt.value.len] != '\0')
            abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Copy into an exact-size heap buffer so ASan catches any overread. */
    uint8_t *buf = malloc(size ? size : 1);
    if (size != 0)
        memcpy(buf, data, size);

    struct tftp_packet pkt;
    if (tftp_packet_decode(&pkt, buf, size) == TFTP_OK) {
        switch (pkt.opcode) {
        case TFTP_OPCODE_RRQ:
        case TFTP_OPCODE_WRQ:
            walk_options(&pkt.body.request.options);
            break;
        case TFTP_OPCODE_OACK:
            walk_options(&pkt.body.oack.options);
            break;
        default:
            break;
        }

        size_t want = tftp_packet_encoded_size(&pkt);
        if (want != size)
            abort();
        uint8_t *out = malloc(size);
        size_t n = 0;
        if (tftp_packet_encode(&pkt, out, size, &n) != TFTP_OK || n != size ||
            memcmp(out, buf, size) != 0)
            abort();
        if (tftp_packet_encode(&pkt, out, size - 1, &n) != TFTP_NOSPACE)
            abort();
        free(out);
    }

    free(buf);
    return 0;
}
