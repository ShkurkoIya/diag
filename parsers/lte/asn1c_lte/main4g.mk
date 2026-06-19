include Makefile.am.libasncodec

LIBS += -lm
CFLAGS += $(ASN_MODULE_CFLAGS) -O0 -g -DASN_PDU_COLLECTION -I.
ASN_LIBRARY ?= libasncodec.a
ASN_PROGRAM_SRCS ?= \
	main4g.c\
	pdu_collection.c

all: $(ASN_LIBRARY) $(ASN_PROGRAM_SRCS:.c=.o)

$(ASN_LIBRARY): $(ASN_MODULE_SRCS:.c=.o)
	$(AR) rcs $@ $(ASN_MODULE_SRCS:.c=.o)

.SUFFIXES:
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(ASN_LIBRARY)
	rm -f $(ASN_MODULE_SRCS:.c=.o) $(ASN_PROGRAM_SRCS:.c=.o)

regen: clean regenerate-from-asn1-source

regenerate-from-asn1-source:
	asn1c -EF ../../Skeletons/ASN_files/LTE/lte-rrc-14.4.0.asn1
	asn1c -fcompound-names -pdu=all -gen-PER ../../Skeletons/ASN_files/LTE/lte-rrc-14.4.0.asn1
	rm converter-example.*

