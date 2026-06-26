#ifndef LTE_H
#define LTE_H
// #include <stdbool.h>
// #include <asn_application.h>
// #include "constr_TYPE.h"

#ifdef __cplusplus
extern "C" {
#endif

int parse4G(uint8_t *packet, size_t size, const char *pduName, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif