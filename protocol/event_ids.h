//
// Created by user on 18.03.2026.
//
#pragma once
#ifndef EVENT_IDS_H
#define EVENT_IDS_H

/*
 * Минимальный набор event ID для DCI маски.
 * Взят из event_defs.h (Qualcomm), только нужные для сбора соседей и
 * отслеживания состояния RRC/MM.
 *
 * Полный файл: event_defs.h (прикреплён к проекту).
 */

// ─── LTE ─────────────────────────────────────────────────────────────────────
#define EVENT_LTE_RRC_STATE_CHANGE              1791   // 0x6FF
#define EVENT_LTE_RRC_NEW_CELL_IND              1795   // 0x703
#define EVENT_LTE_RRC_OUT_OF_SERVICE            1793   // 0x701
#define EVENT_LTE_RRC_RADIO_LINK_FAILURE        1794   // 0x702
#define EVENT_LTE_RRC_IRAT_HO_FROM_EUTRAN       1799   // 0x707
#define EVENT_LTE_RRC_IRAT_HO_FROM_EUTRAN_FAILURE 1800
#define EVENT_LTE_RRC_IRAT_RESEL_FROM_EUTRAN    1801   // 0x709
#define EVENT_LTE_RRC_IRAT_RESEL_FROM_EUTRAN_FAILURE 1802
#define EVENT_LTE_RRC_CELL_RESEL_FAILURE        1796   // 0x704
#define EVENT_LTE_RRC_HO_FAILURE                1797   // 0x705
#define EVENT_LTE_RRC_DL_MSG                    1803   // 0x70B
#define EVENT_LTE_RRC_UL_MSG                    1804   // 0x70C
#define EVENT_LTE_RRC_IRAT_REDIR_FROM_EUTRAN_START 1808
#define EVENT_LTE_RRC_IRAT_REDIR_FROM_EUTRAN_END   1809
#define EVENT_LTE_ML1_STATE_CHANGE              1810   // 0x712
#define EVENT_LTE_RACH_ACCESS_START             1785   // 0x6F9
#define EVENT_LTE_RACH_ACCESS_RESULT            1787   // 0x6FB
#define EVENT_LTE_TIMING_ADVANCE                1782   // 0x6F6

// ─── WCDMA ───────────────────────────────────────────────────────────────────
#define EVENT_WCDMA_L1_STATE                    468    // 0x1D4
#define EVENT_WCDMA_IMSI                        469    // 0x1D5
#define EVENT_WCDMA_NEW_REFERENCE_CELL          481    // 0x1E1
#define EVENT_WCDMA_ASET                        482    // 0x1E2
#define EVENT_WCDMA_INTER_RAT_HANDOVER_START    491    // 0x1EB
#define EVENT_WCDMA_INTER_RAT_HANDOVER_END      492    // 0x1EC
#define EVENT_WCDMA_TO_WCDMA_RESELECTION_START  497    // 0x1F1
#define EVENT_WCDMA_TO_WCDMA_RESELECTION_END    500    // 0x1F4
#define EVENT_WCDMA_TO_GSM_RESELECTION_START    498    // 0x1F2
#define EVENT_WCDMA_TO_GSM_RESELECTION_END      499    // 0x1F3
#define EVENT_WCDMA_L1_SUSPEND                  494    // 0x1EE
#define EVENT_WCDMA_L1_RESUME                   495    // 0x1EF
#define EVENT_WCDMA_LAYER1_MEASUREMENT          493    // 0x1ED
#define EVENT_WCDMA_RACH_ATTEMPT                501    // 0x1F5

// ─── GSM ─────────────────────────────────────────────────────────────────────
#define EVENT_GSM_L1_STATE                      470    // 0x1D6
#define EVENT_GSM_HANDOVER_START                474    // 0x1DA
#define EVENT_GSM_HANDOVER_END                  475    // 0x1DB
#define EVENT_GSM_LINK_FAILURE                  476    // 0x1DC
#define EVENT_GSM_RESELECT_START                477    // 0x1DD
#define EVENT_GSM_RESELECT_END                  478    // 0x1DE
#define EVENT_GSM_CAMP_ATTEMPT_START            479    // 0x1DF
#define EVENT_GSM_CAMP_ATTEMPT_END              486    // 0x1E6
#define EVENT_GSM_CELL_SELECTION_START          487    // 0x1E7
#define EVENT_GSM_CELL_SELECTION_END            488    // 0x1E8
#define EVENT_GSM_RR_IN_SERVICE                 484    // 0x1E4
#define EVENT_GSM_RR_OUT_OF_SERVICE             485    // 0x1E5
#define EVENT_GSM_PAGE_RECEIVED                 483    // 0x1E3
#define EVENT_GSM_PLMN_LIST_START               489    // 0x1E9
#define EVENT_GSM_PLMN_LIST_END                 490    // 0x1EA
#define EVENT_GSM_MESSAGE_SENT                  502    // 0x1F6
#define EVENT_GSM_MESSAGE_RECEIVED              503    // 0x1F7

#endif // EVENT_IDS_H