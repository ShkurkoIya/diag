# diag_scan — C++ source organization

## Directory layout

```
cpp/
├── CMakeLists.txt                 Build config
├── main.cpp                       diag_scan binary entry point
│
├── core/                          Foundational types & utilities
│   ├── diag_common.h              LogRecord, RAT cell types, utilities
│   ├── diag_defs.h                Generic DIAG protocol constants
│   ├── libdiag_loader.h           dlopen wrapper for vendor libdiag.so
│   └── libdiag_loader.cpp
│
├── protocol/                      Qualcomm DIAG protocol definitions
│   ├── diagcmd.h                  Command codes
│   ├── log_codes.h                Log code numeric tables
│   ├── event_defs.h, event_ids.h  Event subsystem definitions
│   ├── diag_log_gsm.h
│   ├── diag_log_umts.h
│   ├── diag_log_wcdma.h
│   ├── diag_lte_rrc_ota.h         LTE RRC OTA on-wire format
│   └── diag_dci_client.h          DCI subscriber API
│
├── parsers/                       RAT parsers
│   ├── qualcomm_log_parser.h      ★ Top-level dispatcher
│   ├── qualcomm_log_parser.cpp
│   │
│   ├── gsm/                       GSM parser
│   │   ├── diag_gsm_log_parser.h
│   │   └── diag_gsm_log_parser.cpp
│   │
│   ├── wcdma/                     WCDMA parser
│   ├── umts/                      UMTS NAS/RRC parser
│   ├── nr/                        NR5G parser
│   │
│   └── lte/                       ★ 3-way LTE split
│       ├── diag_lte_rrc_parser.h       ★ NEW — owns LteCellIdentity
│       ├── diag_lte_rrc_parser.cpp           Handles 0xB0C0/C1/C2
│       │                                     PRIMARY identity source
│       ├── diag_lte_nas_parser.h             Slimmed — EMM/ESM only
│       ├── diag_lte_nas_parser.cpp           Handles 0xB0EA/EC/ED/E0/E2
│       │                                     FALLBACK identity (TAI list)
│       ├── diag_lte_ml1_parser.h             Renamed from log_parser
│       ├── diag_lte_ml1_parser.cpp           Handles 0xB0C4/B18E/B192/B193/B196
│       │                                     ONLY measurements (no identity)
│       ├── item_utils.h                Scat-based wrapper layouts
│       │                                     QcDiagLteRrcServCellInfo_v2/v3
│       │                                     QcDiagLteMib_v1/v2/v17
│       │                                     ItemV25 (0xB0C0 wrapper)
│       ├── lte_rrc_ota_decoder.{h,cpp}  Legacy wrapper decoder (per-version
│       │                                     dispatch table for 0xB0C0)
│       ├── lte_sib1_decoder.{h,cpp}     Hand-written PER decoder for SIB1
│       │                                     (no asn1c dep, ~250 LOC)
│       ├── lte_sib1_decoder_asn1c.{h,cpp}    asn1c-backed alternative
│       │                                     (only when USE_ASN1C_LTE=ON)
│       └── asn1c_lte_bridge.{h,cpp}     Generic asn1c PDU decoder
│
├── modem/                         Cell-lock + EFS file system access
│   ├── diag_cell_lock.h           Lock-to-cell EFS writer
│   ├── diag_efs2.h                EFS2 protocol & helpers
│   ├── efs_browse.cpp             EFS directory browser
│   └── main_lock_additions.cpp    Cell-lock CLI extensions to main
│
├── dci/                           DCI client wrapper
│   └── diag_dci_client.cpp
│
├── jni/                           Android JNI bridge
│   ├── diag_jni.cpp               Java→C entry points
│   └── diag_jni_lock_additions.cpp  Lock-related JNI methods
│
└── asn1/                          Reference ASN.1 specifications
    ├── EUTRA-RRC-Definitions.asn
    ├── EUTRA-InterNodeDefinitions.asn
    ├── lte-rrc-19_0_0.asn1
    └── regen_asn1c_lte.sh         Regen helper for asn1c output
```

## Responsibility matrix (LTE)

The most important architectural change is the 3-way LTE split. Source of
truth for each LTE field:

| Field            | Source                  | Parser            |
|------------------|-------------------------|-------------------|
| EARFCN, PCI      | 0xB0C2 + 0xB193         | RRC + ML1         |
| **MCC, MNC**     | **0xB0C2 (primary)**    | **RRC**           |
|                  | NAS Attach (fallback)   | NAS               |
| **CID**          | **0xB0C2**              | **RRC**           |
| **TAC**          | **0xB0C2 (primary)**    | **RRC**           |
|                  | NAS Attach (fallback)   | NAS               |
| Band, BW         | 0xB0C2                  | RRC               |
| **RSRP, RSRQ**   | **0xB193, 0xB196**      | **ML1**           |
| RSSI, SINR       | 0xB193, 0xB196          | ML1               |
| SFN, PHICH       | 0xB0C1                  | RRC               |

## Cross-reference logic

`QualcommLogParser::merge_lte_identity()` runs after each LTE packet and:

1. Pulls authoritative identity from `lte_rrc_.identity()`
2. Walks ML1 neighbor list (measurements with PCI+EARFCN only)
3. Finds entries matching RRC.earfcn + RRC.pci → marks as serving + stamps
   them with full identity (MCC/MNC/CID/TAC)
4. Other ML1 entries remain as measurement-only neighbors

This eliminates the previous bug where ML1 ML1 parser falsely emitted
"serving cell" entries with PCI=1 (a counter field misread).

## Build

```bash
cd cpp/
cmake -B build .
cmake --build build -j$(nproc)
# Output: build/libdiag_scan_exe.so
```

Optional asn1c-backed LTE decoder:
```bash
# After copying reference asn1c output into parsers/lte/asn1c_lte/
cmake -B build -DUSE_ASN1C_LTE=ON .
cmake --build build -j$(nproc)
```

## Integration with Android Project

The output `libdiag_scan_exe.so` goes into the Android app's
`jniLibs/arm64-v8a/` directory. At runtime, `DiagManager.kt` copies it to
`/data/local/tmp/diag_scan` and chmod +x's it, then launches via `su -c`.

For direct JNI calls (without exec'ing a binary), enable the optional
target:
```bash
cmake -B build -DBUILD_JNI_LIB=ON .
# Output: also builds libdiag_jni.so
```

## What was removed in the refactor

- ❌ Duplicate parse_rrc_ota in DiagLteLogParser (RRC code now only in RRC parser)
- ❌ Duplicate handled_log_codes for 0xB0C0 (was in both NAS and LOG)
- ❌ ML1 parser claiming serving=true with wrong PCI (now uses cross-reference)
- ❌ Mis-named LOG_LTE_ML1_SERVING_CELL_MEAS_C define (was 0xB0C2 — that's RRC, not ML1)
- ❌ `print_b0c0_stats()` method in ML1 (RRC parser logs SUMMARY internally)

## What remains the same

- ✅ item_utils.h structs and parsers (working scat-based code)
- ✅ lte_rrc_ota_decoder + lte_sib1_decoder + asn1c_lte_bridge (alternative paths)
- ✅ All GSM/WCDMA/UMTS/NR parsers
- ✅ JNI bridge interface
- ✅ Cell-lock and EFS2 code
- ✅ Output binary name `libdiag_scan_exe.so`
