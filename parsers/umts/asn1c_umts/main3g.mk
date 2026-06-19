include Makefile.am.libasncodec

LIBS += -lm
CFLAGS += $(ASN_MODULE_CFLAGS) -Os -g -DASN_PDU_COLLECTION -I.
ASN_LIBRARY ?= libasncodec.a
ASN_PROGRAM_SRCS ?= \
	main3g.c\
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

regen: regenerate-from-asn1-source

regenerate-from-asn1-source:
	asn1c -EF ../../Skeletons/ASN_files/UMTS/rrc-25.331-i00.asn1
	asn1c -fcompound-names -pdu=all ../../Skeletons/ASN_files/UMTS/rrc-25.331-i00.asn1
	rm converter-example.*

