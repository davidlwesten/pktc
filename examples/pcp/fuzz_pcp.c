/* libFuzzer harness: decode never reads out of bounds, and re-encoding is
 * stable. Reserved bits and padding are ignored on decode and written as
 * zeros, so the first encode may differ from the input, but only there: it
 * has the same length, decodes again, and encodes to identical bytes.
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       fuzz_pcp.c pcp_gen.c -o fuzz_pcp
 */
#include <stdlib.h>
#include <string.h>

#include "pcp_gen.h"

static volatile uint8_t sink;

static void walk_options(const struct pcp_option_list *list) {
    struct pcp_option_iter it;
    struct pcp_option opt;
    pcp_option_iter_init(&it, list);
    while (pcp_option_iter_next(&it, &opt))
        if (opt.data.len != 0)
            sink = opt.data.ptr[opt.data.len - 1];  /* ASan checks the view */
}

#define FUZZ_MESSAGE(type)                                                  \
    do {                                                                    \
        struct pcp_##type msg, again;                                       \
        if (pcp_##type##_decode(&msg, buf, size) != PCP_OK)                 \
            break;                                                          \
        switch (msg.opcode) {                                               \
        case PCP_OPCODE_ANNOUNCE:                                           \
            walk_options(&msg.body.announce.options);                       \
            break;                                                          \
        case PCP_OPCODE_MAP:                                                \
            walk_options(&msg.body.map.options);                            \
            break;                                                          \
        case PCP_OPCODE_PEER:                                               \
            walk_options(&msg.body.peer.options);                           \
            break;                                                          \
        default:                                                            \
            break;                                                          \
        }                                                                   \
        if (pcp_##type##_encoded_size(&msg) != size)                        \
            abort();                                                        \
        uint8_t *a = malloc(size), *b = malloc(size);                       \
        size_t na = 0, nb = 0;                                              \
        if (pcp_##type##_encode(&msg, a, size, &na) != PCP_OK || na != size) \
            abort();                                                        \
        if (pcp_##type##_encode(&msg, a, size - 1, &nb) != PCP_NOSPACE)    \
            abort();                                                        \
        if (pcp_##type##_decode(&again, a, na) != PCP_OK)                   \
            abort();                                                        \
        if (pcp_##type##_encode(&again, b, size, &nb) != PCP_OK ||          \
            nb != na || memcmp(a, b, na) != 0)                              \
            abort();                                                        \
        free(a);                                                            \
        free(b);                                                            \
    } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Copy into an exact-size heap buffer so ASan catches any overread. */
    uint8_t *buf = malloc(size ? size : 1);
    if (size != 0)
        memcpy(buf, data, size);
    FUZZ_MESSAGE(request);
    FUZZ_MESSAGE(response);
    free(buf);
    return 0;
}
