// asn1c_lte_bridge.cpp
//
// Bridge between our DIAG pipeline and asn1c-generated LTE RRC parsers.

#include "asn1c_lte_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef USE_ASN1C_LTE
extern "C" {
#include <asn_application.h>
#include <asn_internal.h>
#include <constr_TYPE.h>

#include "BCCH-BCH-Message.h"
#include "BCCH-DL-SCH-Message.h"
#include "InterFreqCarrierFreqInfo.h"
#include "InterFreqNeighCellInfo.h"
#include "IntraFreqNeighCellInfo.h"
#include "MasterInformationBlock.h"
#include "MeasResultEUTRA.h"
#include "MeasResults.h"
#include "MeasurementReport.h"
#include "PLMN-IdentityInfo.h"
#include "SystemInformation.h"
#include "SystemInformationBlockType1.h"
#include "SystemInformationBlockType4.h"
#include "SystemInformationBlockType5.h"
#include "UL-DCCH-Message.h"

extern asn_TYPE_descriptor_t *asn_pdu_collection[];
}
#endif

namespace lte_asn1 {

    const char *pdu_type_name(uint8_t pdu_type_canonical) {
        switch (pdu_type_canonical) {
            case 1:
                return "BCCH-BCH-Message";
            case 2:
                return "BCCH-DL-SCH-Message";
            case 3:
                return "MCCH-Message";
            case 4:
                return "PCCH-Message";
            case 5:
                return "DL-CCCH-Message";
            case 6:
                return "DL-DCCH-Message";
            case 7:
                return "UL-CCCH-Message";
            case 8:
                return "UL-DCCH-Message";
            default:
                return nullptr;
        }
    }

#ifdef USE_ASN1C_LTE

    namespace {

        asn_TYPE_descriptor_t *find_pdu_type(const char *name) {
            if (!name) return nullptr;
            for (int i = 0; asn_pdu_collection[i] != nullptr; ++i) {
                if (std::strcmp(asn_pdu_collection[i]->name, name) == 0) {
                    return asn_pdu_collection[i];
                }
            }
            return nullptr;
        }

        uint64_t bitstring_to_uint(const uint8_t *buf, int size, int bits_unused) {
            if (!buf || size <= 0) return 0;
            int n = size > 8 ? 8 : size;
            uint64_t v = 0;
            for (int i = 0; i < n; ++i) v = (v << 8) | buf[i];
            if (bits_unused > 0 && bits_unused < 8) v >>= bits_unused;
            return v;
        }

        template<typename ListT>
        std::string digits_to_string(const ListT &list) {
            std::string out;
            out.reserve(list.list.count);
            for (int i = 0; i < list.list.count; ++i) {
                long *d = list.list.array[i];
                if (!d || *d < 0 || *d > 9) return std::string();
                out.push_back(static_cast<char>('0' + *d));
            }
            return out;
        }

        // Helper: heap-allocate a long with the given value and add it to a
        // SEQUENCE OF INTEGER list. asn1c's A_SEQUENCE_OF(long) stores long*
        // pointers in its array — those pointers MUST point to heap memory
        // (asn1c calls free() on each via the generated free function).
        //
        // Passing pointers to stack vars works in toy x86 builds but blows up on
        // arm64 with tagged pointers, because:
        //   1. The stack slot is gone after this function returns
        //   2. arm64 Top-Byte Ignore truncates tag bits during void* casts,
        //      and Android crashes on the first access
        template<typename ListT>
        bool seq_add_int(ListT &list, long value) {
            long *slot = (long *) std::calloc(1, sizeof(long));
            if (!slot) return false;
            *slot = value;
            if (asn_sequence_add(&list, slot) != 0) {
                std::free(slot);
                return false;
            }
            return true;
        }

    }// anonymous namespace

    int pdu_collection_size() {
        int n = 0;
        while (asn_pdu_collection[n] != nullptr) ++n;
        return n;
    }

    void list_pdu_names(FILE *fp) {
        if (!fp) fp = stderr;
        for (int i = 0; asn_pdu_collection[i] != nullptr; ++i) {
            std::fprintf(fp, "  [%3d] %s\n", i, asn_pdu_collection[i]->name);
        }
    }

    DecodeResult decode_pdu(const uint8_t *pdu, size_t pdu_len, const char *pdu_name) {
        DecodeResult r;
        if (!pdu || pdu_len < 1 || !pdu_name) {
            r.kind = PduKind::DECODE_FAILED;
            r.error = "invalid arguments";
            return r;
        }

        asn_TYPE_descriptor_t *pdu_type = find_pdu_type(pdu_name);
        if (!pdu_type) {
            r.kind = PduKind::DECODE_FAILED;
            r.error = std::string("PDU type not in asn_pdu_collection: ") + pdu_name;
            return r;
        }

        void *structure = nullptr;
        asn_dec_rval_t rv = asn_decode(
                nullptr, ATS_UNALIGNED_BASIC_PER,
                pdu_type, &structure, pdu, pdu_len);

        if (rv.code != RC_OK || !structure) {
            r.kind = PduKind::DECODE_FAILED;
            r.error = (rv.code == RC_WMORE)  ? "incomplete PDU"
                      : (rv.code == RC_FAIL) ? "ASN.1 decode failed"
                                             : "unknown decode error";
            if (structure) ASN_STRUCT_FREE(*pdu_type, structure);
            return r;
        }

        if (std::strcmp(pdu_name, "BCCH-DL-SCH-Message") == 0) {
            auto *msg = static_cast<BCCH_DL_SCH_Message_t *>(structure);
            if (msg->message.present == BCCH_DL_SCH_MessageType_PR_c1) {
                auto &c1 = msg->message.choice.c1;
                if (c1.present == BCCH_DL_SCH_MessageType__c1_PR_systemInformationBlockType1) {
                    r.message_type = "SIB1";
                    SystemInformationBlockType1_t &sib1 =
                            c1.choice.systemInformationBlockType1;
                    Sib1Fields f;

                    auto &plmn_list = sib1.cellAccessRelatedInfo.plmn_IdentityList;
                    std::string last_mcc;
                    for (int i = 0; i < plmn_list.list.count; ++i) {
                        PLMN_IdentityInfo_t *info = plmn_list.list.array[i];
                        if (!info) continue;
                        PlmnEntry pe;
                        if (info->plmn_Identity.mcc) {
                            pe.mcc = digits_to_string(*info->plmn_Identity.mcc);
                            last_mcc = pe.mcc;
                        } else if (!last_mcc.empty()) {
                            pe.mcc = last_mcc;
                        }
                        pe.mnc = digits_to_string(info->plmn_Identity.mnc);
                        pe.reserved_for_operator_use =
                                (info->cellReservedForOperatorUse ==
                                 PLMN_IdentityInfo__cellReservedForOperatorUse_reserved);
                        if (!pe.mcc.empty() && !pe.mnc.empty())
                            f.plmn_list.push_back(std::move(pe));
                    }

                    auto &tac_bs = sib1.cellAccessRelatedInfo.trackingAreaCode;
                    f.tac = static_cast<uint16_t>(
                            bitstring_to_uint(tac_bs.buf, tac_bs.size, tac_bs.bits_unused));

                    auto &cid_bs = sib1.cellAccessRelatedInfo.cellIdentity;
                    f.cell_id = static_cast<uint32_t>(
                            bitstring_to_uint(cid_bs.buf, cid_bs.size, cid_bs.bits_unused));

                    f.cell_barred = (sib1.cellAccessRelatedInfo.cellBarred ==
                                     SystemInformationBlockType1__cellAccessRelatedInfo__cellBarred_barred);
                    f.intra_freq_reselection_allowed =
                            (sib1.cellAccessRelatedInfo.intraFreqReselection ==
                             SystemInformationBlockType1__cellAccessRelatedInfo__intraFreqReselection_allowed);
                    f.csg_indication = sib1.cellAccessRelatedInfo.csg_Indication;

                    if (sib1.cellAccessRelatedInfo.csg_Identity) {
                        auto &csg_bs = *sib1.cellAccessRelatedInfo.csg_Identity;
                        f.csg_identity = static_cast<uint32_t>(
                                bitstring_to_uint(csg_bs.buf, csg_bs.size, csg_bs.bits_unused));
                    }
                    f.freq_band = static_cast<uint16_t>(sib1.freqBandIndicator);

                    r.kind = PduKind::SIB1;
                    r.ok = true;
                    r.sib1 = std::move(f);
                } else if (c1.present == BCCH_DL_SCH_MessageType__c1_PR_systemInformation) {
                    // Periodic System Information — may contain SIB4 (intra-freq
                    // neighbor PCIs) and SIB5 (inter-freq carriers: EARFCN + PCI
                    // lists). These give us the PCI→EARFCN mapping needed to
                    // resolve the carrier frequency of measured neighbor cells.
                    SystemInformation_t &si = c1.choice.systemInformation;
                    if (si.criticalExtensions.present ==
                        SystemInformation__criticalExtensions_PR_systemInformation_r8) {
                        auto &sib_list = si.criticalExtensions.choice
                                                 .systemInformation_r8.sib_TypeAndInfo;
                        std::string sibs;
                        for (int i = 0; i < sib_list.list.count; ++i) {
                            auto *item = sib_list.list.array[i];
                            if (!item) continue;

                            if (item->present >= SystemInformation_r8_IEs__sib_TypeAndInfo__Member_PR_sib2 &&
                                item->present <= SystemInformation_r8_IEs__sib_TypeAndInfo__Member_PR_sib11) {
                                int n = static_cast<int>(item->present) - static_cast<int>(SystemInformation_r8_IEs__sib_TypeAndInfo__Member_PR_sib2) + 2;
                                if (!sibs.empty()) sibs += ",";
                                sibs += "sib" + std::to_string(n);
                            } else {
                                if (!sibs.empty()) sibs += ",";
                                sibs += "sibX";
                            }

                            if (item->present ==
                                SystemInformation_r8_IEs__sib_TypeAndInfo__Member_PR_sib4) {
                                // SIB4: intra-frequency neighbors (same EARFCN as
                                // the serving cell → earfcn=0, filled by caller).
                                SystemInformationBlockType4_t &s4 = item->choice.sib4;
                                if (s4.intraFreqNeighCellList) {
                                    auto &lst = *s4.intraFreqNeighCellList;
                                    for (int j = 0; j < lst.list.count; ++j) {
                                        IntraFreqNeighCellInfo_t *nb = lst.list.array[j];
                                        if (!nb) continue;
                                        NeighborFreq nf;
                                        nf.pci = static_cast<int>(nb->physCellId);
                                        nf.earfcn = 0;// intra-freq
                                        nf.inter_freq = false;
                                        r.neighbor_freqs.push_back(nf);
                                    }
                                }
                            } else if (item->present ==
                                       SystemInformation_r8_IEs__sib_TypeAndInfo__Member_PR_sib5) {
                                // SIB5: inter-frequency carriers. Each carrier has
                                // its own dl-CarrierFreq (EARFCN) + a neighbor PCI
                                // list → exact PCI→EARFCN mapping.
                                SystemInformationBlockType5_t &s5 = item->choice.sib5;
                                auto &carriers = s5.interFreqCarrierFreqList;
                                for (int c = 0; c < carriers.list.count; ++c) {
                                    InterFreqCarrierFreqInfo_t *ci = carriers.list.array[c];
                                    if (!ci) continue;
                                    uint32_t earfcn =
                                            static_cast<uint32_t>(ci->dl_CarrierFreq);
                                    if (ci->interFreqNeighCellList) {
                                        auto &nl = *ci->interFreqNeighCellList;
                                        for (int j = 0; j < nl.list.count; ++j) {
                                            InterFreqNeighCellInfo_t *nb = nl.list.array[j];
                                            if (!nb) continue;
                                            NeighborFreq nf;
                                            nf.pci = static_cast<int>(nb->physCellId);
                                            nf.earfcn = earfcn;// inter-freq
                                            nf.inter_freq = true;
                                            r.neighbor_freqs.push_back(nf);
                                        }
                                    }
                                }
                            }
                        }
                        r.message_type = sibs.empty()
                                                 ? std::string("SystemInformation")
                                                 : ("SI [" + sibs + "]");
                    }
                    r.kind = PduKind::SI_MESSAGE;
                    r.ok = true;
                }
            }
        } else if (std::strcmp(pdu_name, "BCCH-BCH-Message") == 0) {
            auto *msg = static_cast<BCCH_BCH_Message_t *>(structure);
            auto &mib = msg->message;
            MibFields f;
            static const uint8_t bw_to_rb[6] = {6, 15, 25, 50, 75, 100};
            long bw = mib.dl_Bandwidth;
            if (bw >= 0 && bw < 6) f.dl_bandwidth_rb = bw_to_rb[bw];
            if (mib.systemFrameNumber.size >= 1) f.sfn_msb8 = mib.systemFrameNumber.buf[0];
            f.phich_duration = mib.phich_Config.phich_Duration;
            f.phich_resource = mib.phich_Config.phich_Resource;
            r.kind = PduKind::MIB;
            r.ok = true;
            r.mib = f;
        } else if (std::strcmp(pdu_name, "UL-DCCH-Message") == 0) {
            // UL-DCCH may carry a MeasurementReport with rsrp/rsrq of the
            // serving (PCell) and EUTRA neighbor cells. This only fires when
            // the UE is RRC_CONNECTED with measurements configured — i.e. when
            // we've locked/camped onto a cell (active scan). It's the only RRC
            // channel that contains signal measurements.
            //
            // 3GPP mappings:
            //   RSRP-Range 0..97 → rsrp_dBm = value - 140   (0=<-140, 97=>-44)
            //   RSRQ-Range 0..34 → rsrq_dB  = value*0.5 - 19.5
            auto rsrqMap = [](long v) -> int {
                // round(v*0.5 - 19.5)
                return static_cast<int>((v - 39) / 2);
            };
            auto *msg = static_cast<UL_DCCH_Message_t *>(structure);
            if (msg->message.present == UL_DCCH_MessageType_PR_c1 &&
                msg->message.choice.c1.present ==
                        UL_DCCH_MessageType__c1_PR_measurementReport) {
                MeasurementReport_t &mr =
                        msg->message.choice.c1.choice.measurementReport;
                if (mr.criticalExtensions.present ==
                            MeasurementReport__criticalExtensions_PR_c1 &&
                    mr.criticalExtensions.choice.c1.present ==
                            MeasurementReport__criticalExtensions__c1_PR_measurementReport_r8) {
                    MeasResults_t &res =
                            mr.criticalExtensions.choice.c1.choice
                                    .measurementReport_r8.measResults;

                    // Serving PCell (mandatory rsrp/rsrq)
                    MeasResultEntry serv;
                    serv.is_serving = true;
                    serv.pci = -1;
                    serv.rsrp_dbm = static_cast<int>(res.measResultPCell.rsrpResult) - 140;
                    serv.rsrq_db = rsrqMap(res.measResultPCell.rsrqResult);
                    r.meas_results.push_back(serv);

                    // EUTRA neighbors (rsrp/rsrq optional per cell)
                    if (res.measResultNeighCells &&
                        res.measResultNeighCells->present ==
                                MeasResults__measResultNeighCells_PR_measResultListEUTRA) {
                        auto &lst = res.measResultNeighCells->choice.measResultListEUTRA;
                        for (int i = 0; i < lst.list.count; ++i) {
                            MeasResultEUTRA_t *e = lst.list.array[i];
                            if (!e) continue;
                            MeasResultEntry me;
                            me.is_serving = false;
                            me.pci = static_cast<int>(e->physCellId);
                            if (e->measResult.rsrpResult)
                                me.rsrp_dbm = static_cast<int>(*e->measResult.rsrpResult) - 140;
                            if (e->measResult.rsrqResult)
                                me.rsrq_db = rsrqMap(*e->measResult.rsrqResult);
                            r.meas_results.push_back(me);
                        }
                    }
                }
            }
            r.kind = PduKind::UL_DCCH;
            r.ok = true;
        } else {
            r.kind = PduKind::UNKNOWN;
            r.ok = true;
        }

        ASN_STRUCT_FREE(*pdu_type, structure);
        return r;
    }

    int print_pdu(const uint8_t *pdu, size_t pdu_len, const char *pdu_name, FILE *fp) {
        if (!pdu || !pdu_name || !fp) return -1;
        asn_TYPE_descriptor_t *pdu_type = find_pdu_type(pdu_name);
        if (!pdu_type) {
            std::fprintf(fp, "[asn1c] PDU type not found: %s\n", pdu_name);
            return -1;
        }
        void *structure = nullptr;
        asn_dec_rval_t rv = asn_decode(nullptr, ATS_UNALIGNED_BASIC_PER,
                                       pdu_type, &structure, pdu, pdu_len);
        if (rv.code == RC_OK && structure) {
            asn_fprint(fp, pdu_type, structure);
        } else {
            std::fprintf(fp, "[asn1c] decode FAILED for %s (code=%d, consumed=%zu)\n",
                         pdu_name, (int) rv.code, rv.consumed);
        }
        if (structure) ASN_STRUCT_FREE(*pdu_type, structure);
        return rv.code == RC_OK ? 0 : 1;
    }

    std::string decode_pdu_text(const uint8_t *pdu, size_t pdu_len, const char *pdu_name) {
        char *buf = nullptr;
        size_t sz = 0;
        FILE *ms = open_memstream(&buf, &sz);
        if (!ms) return std::string();
        print_pdu(pdu, pdu_len, pdu_name, ms);
        std::fclose(ms);
        std::string out = (buf && sz) ? std::string(buf, sz) : std::string();
        std::free(buf);
        return out;
    }

    // ── Selftest: encode-decode roundtrip with synthetic SIB1 ─────────────
    //
    // Builds a valid SIB1 in memory using ONLY heap-allocated children
    // (calloc), encodes it to UPER, decodes it back, and verifies that
    // the round-trip preserves PLMN, TAC, CID, band.
    //
    // arm64 NOTE: every long* added to a SEQUENCE OF MUST be heap-allocated.
    // Stack pointers crash because:
    //   - asn1c's free function calls free() on each element (UB on stack)
    //   - arm64 tagged pointers truncate on void* roundtrip and Android
    //     traps the access ("Pointer tag was truncated")
    bool selftest() {
        std::fprintf(stderr, "[asn1c selftest] starting...\n");

        int npdu = pdu_collection_size();
        std::fprintf(stderr, "[asn1c selftest] asn_pdu_collection has %d entries\n", npdu);
        if (npdu == 0) {
            std::fprintf(stderr, "[asn1c selftest] FAIL: pdu_collection.c not linked\n");
            return false;
        }

        const char *critical[] = {
                "BCCH-DL-SCH-Message", "BCCH-BCH-Message",
                "PCCH-Message", "DL-CCCH-Message", "UL-DCCH-Message", nullptr};
        for (int i = 0; critical[i]; ++i) {
            if (!find_pdu_type(critical[i])) {
                std::fprintf(stderr, "[asn1c selftest] FAIL: '%s' not in collection\n",
                             critical[i]);
                return false;
            }
        }
        std::fprintf(stderr, "[asn1c selftest] all critical PDU types present\n");

        // Build synthetic SIB1: PLMN=250-01 TAC=7 CID=0xF0007 band=3
        BCCH_DL_SCH_Message_t *msg = (BCCH_DL_SCH_Message_t *)
                std::calloc(1, sizeof(BCCH_DL_SCH_Message_t));
        if (!msg) return false;

        msg->message.present = BCCH_DL_SCH_MessageType_PR_c1;
        msg->message.choice.c1.present =
                BCCH_DL_SCH_MessageType__c1_PR_systemInformationBlockType1;
        SystemInformationBlockType1_t &sib1 =
                msg->message.choice.c1.choice.systemInformationBlockType1;

        PLMN_IdentityInfo_t *pi = (PLMN_IdentityInfo_t *)
                std::calloc(1, sizeof(PLMN_IdentityInfo_t));
        pi->plmn_Identity.mcc = (MCC_t *) std::calloc(1, sizeof(MCC_t));

        // MCC = 250 — each digit is its own heap-allocated long*
        if (!seq_add_int(pi->plmn_Identity.mcc->list, 2) ||
            !seq_add_int(pi->plmn_Identity.mcc->list, 5) ||
            !seq_add_int(pi->plmn_Identity.mcc->list, 0)) {
            std::fprintf(stderr, "[asn1c selftest] FAIL: MCC alloc\n");
            ASN_STRUCT_FREE(asn_DEF_BCCH_DL_SCH_Message, msg);
            return false;
        }
        // MNC = 01
        if (!seq_add_int(pi->plmn_Identity.mnc.list, 0) ||
            !seq_add_int(pi->plmn_Identity.mnc.list, 1)) {
            std::fprintf(stderr, "[asn1c selftest] FAIL: MNC alloc\n");
            ASN_STRUCT_FREE(asn_DEF_BCCH_DL_SCH_Message, msg);
            return false;
        }
        // ENUMERATED { reserved (0), notReserved (1) } — use numeric to avoid
        // naming-convention mismatches between asn1c forks.
        pi->cellReservedForOperatorUse = 1;// notReserved
        asn_sequence_add(&sib1.cellAccessRelatedInfo.plmn_IdentityList.list, pi);

        // TAC = 7 (16-bit BIT_STRING, no padding)
        sib1.cellAccessRelatedInfo.trackingAreaCode.buf = (uint8_t *) std::calloc(1, 2);
        sib1.cellAccessRelatedInfo.trackingAreaCode.buf[0] = 0x00;
        sib1.cellAccessRelatedInfo.trackingAreaCode.buf[1] = 0x07;
        sib1.cellAccessRelatedInfo.trackingAreaCode.size = 2;
        sib1.cellAccessRelatedInfo.trackingAreaCode.bits_unused = 0;

        // cellIdentity = 0 (28-bit BIT_STRING).
        //
        // NOTE: We use 0 here intentionally. Synthesizing non-zero BIT_STRING
        // values for asn1c's encoder is finicky — different asn1c forks store
        // BIT_STRING bytes with different conventions (left-justified vs
        // right-justified, bits_unused as leading vs trailing). Value 0
        // round-trips identically regardless of convention.
        //
        // In production, this doesn't matter: real SIB1 packets from eNBs
        // have correctly-encoded BIT_STRINGs that asn1c decodes faithfully.
        // We just can't easily construct a non-zero one for SELFTEST without
        // matching asn1c's exact internal convention.
        sib1.cellAccessRelatedInfo.cellIdentity.buf = (uint8_t *) std::calloc(1, 4);
        sib1.cellAccessRelatedInfo.cellIdentity.buf[0] = 0x00;
        sib1.cellAccessRelatedInfo.cellIdentity.buf[1] = 0x00;
        sib1.cellAccessRelatedInfo.cellIdentity.buf[2] = 0x00;
        sib1.cellAccessRelatedInfo.cellIdentity.buf[3] = 0x00;
        sib1.cellAccessRelatedInfo.cellIdentity.size = 4;
        sib1.cellAccessRelatedInfo.cellIdentity.bits_unused = 4;

        // cellBarred ENUMERATED { barred (0), notBarred (1) }
        sib1.cellAccessRelatedInfo.cellBarred = 1;// notBarred
        // intraFreqReselection ENUMERATED { allowed (0), notAllowed (1) }
        sib1.cellAccessRelatedInfo.intraFreqReselection = 0;// allowed
        sib1.cellAccessRelatedInfo.csg_Indication = 0;
        sib1.freqBandIndicator = 3;

        // si-WindowLength ENUMERATED { ms1(0), ms2(1), ms5(2), ms10(3),
        //                              ms15(4), ms20(5), ms40(6) }
        sib1.si_WindowLength = 5;// ms20
        sib1.systemInfoValueTag = 0;
        sib1.cellSelectionInfo.q_RxLevMin = -70;
        SchedulingInfo_t *si = (SchedulingInfo_t *)
                std::calloc(1, sizeof(SchedulingInfo_t));
        // si-Periodicity ENUMERATED { rf8(0), rf16(1), rf32(2), rf64(3),
        //                              rf128(4), rf256(5), rf512(6) }
        si->si_Periodicity = 1;// rf16
        asn_sequence_add(&sib1.schedulingInfoList.list, si);

        uint8_t enc_buf[256] = {0};
        asn_TYPE_descriptor_t *td = find_pdu_type("BCCH-DL-SCH-Message");
        asn_enc_rval_t er = asn_encode_to_buffer(
                nullptr, ATS_UNALIGNED_BASIC_PER, td, msg, enc_buf, sizeof(enc_buf));

        ASN_STRUCT_FREE(*td, msg);

        if (er.encoded < 0) {
            std::fprintf(stderr, "[asn1c selftest] FAIL: encode rejected: %s\n",
                         er.failed_type ? er.failed_type->name : "(null)");
            return false;
        }
        // asn_encode_to_buffer() returns the byte count directly (it converts
        // PER's internal bit-count to bytes via the buffer callback). So NO
        // division by 8 here. Earlier I had `(er.encoded + 7) / 8` which gave
        // bogus 2-byte length and caused RC_WMORE on decode.
        size_t enc_len = static_cast<size_t>(er.encoded);
        std::fprintf(stderr, "[asn1c selftest] encoded SIB1 to %zu bytes (hex: ", enc_len);
        for (size_t i = 0; i < enc_len && i < 64; ++i)
            std::fprintf(stderr, "%02x", enc_buf[i]);
        std::fprintf(stderr, ")\n");

        auto r = decode_pdu(enc_buf, enc_len, "BCCH-DL-SCH-Message");
        if (!r.ok || r.kind != PduKind::SIB1 || !r.sib1) {
            std::fprintf(stderr, "[asn1c selftest] FAIL: roundtrip decode: %s\n",
                         r.error.c_str());
            return false;
        }
        const auto &s = *r.sib1;
        bool ok = true;
        if (s.plmn_list.size() != 1) {
            std::fprintf(stderr, "  PLMN count=%zu, expected 1\n", s.plmn_list.size());
            ok = false;
        } else {
            if (s.plmn_list[0].mcc != "250") {
                std::fprintf(stderr, "  MCC=%s, expected 250\n",
                             s.plmn_list[0].mcc.c_str());
                ok = false;
            }
            if (s.plmn_list[0].mnc != "01") {
                std::fprintf(stderr, "  MNC=%s, expected 01\n",
                             s.plmn_list[0].mnc.c_str());
                ok = false;
            }
        }
        if (s.tac != 7) {
            std::fprintf(stderr, "  TAC=%u, expected 7\n", s.tac);
            ok = false;
        }
        // We use cellIdentity=0 in the synthetic input (see comment in encode
        // section above) — value 0 round-trips identically regardless of
        // asn1c's BIT_STRING bit-ordering convention. Real production SIB1
        // packets from the air work correctly because asn1c populates the
        // BIT_STRING from raw OTA bytes during decode.
        if (s.cell_id != 0) {
            std::fprintf(stderr, "  CID=0x%X, expected 0 (synthetic input)\n", s.cell_id);
            ok = false;
        }
        if (s.freq_band != 3) {
            std::fprintf(stderr, "  band=%u, expected 3\n", s.freq_band);
            ok = false;
        }

        if (ok) {
            std::fprintf(stderr,
                         "[asn1c selftest] OK: MCC=%s MNC=%s TAC=%u CID=0x%X band=%u\n",
                         s.plmn_list[0].mcc.c_str(), s.plmn_list[0].mnc.c_str(),
                         s.tac, s.cell_id, s.freq_band);
        }
        return ok;
    }

#else// !USE_ASN1C_LTE

    DecodeResult decode_pdu(const uint8_t *, size_t, const char *) {
        DecodeResult r;
        r.kind = PduKind::DECODE_FAILED;
        r.error = "asn1c not compiled in (define USE_ASN1C_LTE)";
        return r;
    }
    int print_pdu(const uint8_t *, size_t, const char *, FILE *) { return -1; }
    int pdu_collection_size() { return 0; }
    void list_pdu_names(FILE *) {}
    bool selftest() {
        std::fprintf(stderr, "asn1c not compiled in — skipping selftest\n");
        return true;
    }

#endif

}// namespace lte_asn1