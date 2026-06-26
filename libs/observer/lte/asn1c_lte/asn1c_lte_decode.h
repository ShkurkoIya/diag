/*
 * asn1c_lte_decode.h — header for asn1c LTE PDU decode adapter.
 *
 * Place alongside asn1c_lte_decode.c in src/asn1c_lte/ directory.
 *
 * Used from C++ via:
 *     extern "C" {
 *         #include "asn1c_lte_decode.h"
 *     }
 */

#ifndef ASN1C_LTE_DECODE_H
#define ASN1C_LTE_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Forward-declare so we don't pull in all of asn_application.h here.
 * The opaque pointer is fine for header-level use; consumers that need
 * to walk structure fields will include the relevant generated header
 * (e.g. BCCH-DL-SCH-Message.h) themselves. */
struct asn_TYPE_descriptor_s;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct decoded_pdu {
    void *structure;                    /* opaque — cast to specific type */
    struct asn_TYPE_descriptor_s *type; /* descriptor for ASN_STRUCT_FREE */
    int success;                        /* 1 = decode OK, 0 = failed */
    size_t consumed;                    /* bytes consumed when success */
    const char *error;                  /* error message when !success */
} decoded_pdu_t;

/**
 * Decode a PDU by name using UNALIGNED PER (UPER).
 * Returns decoded_pdu_t with success=1 on success.
 * Caller MUST call free_pdu() on the result regardless of success/fail.
 *
 * Common pdu_name values for LTE cell discovery:
 *   "BCCH-DL-SCH-Message" — broadcast channel (carries SIB1, MIB-derived)
 *   "BCCH-BCH-Message"    — primary broadcast channel (MIB)
 *   "PCCH-Message"        — paging channel
 */
decoded_pdu_t decode_pdu_by_name(const char *pdu_name,
                                 const uint8_t *data,
                                 size_t size);

/** Free decoded structure. Safe to call on failed-decode results. */
void free_pdu(decoded_pdu_t *pdu);

/** Pretty-print decoded PDU to a FILE — for debugging. */
void print_pdu(const decoded_pdu_t *pdu, FILE *out);

/** List all PDU types in asn_pdu_collection[]. Returns count. */
int list_pdu_types(FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* ASN1C_LTE_DECODE_H */
