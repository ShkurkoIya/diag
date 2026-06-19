#ifndef UMTS_H
#define UMTS_H


#ifdef __cplusplus
extern "C"
{
#endif

    int parse3G(uint8_t *packet, size_t size, const char* pduName, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif