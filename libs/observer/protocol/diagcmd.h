#pragma once
#ifndef DIAGCMD_H
#define DIAGCMD_H
#define DIAG_VERNO_F 0x00// 0
/* Mobile Station ESN Request/Response        */
#define DIAG_ESN_F 0x01// 1
/* 2-11 Obsolete                 */
/* DMSS status Request/Response               */
#define DIAG_STATUS_F 0x0C// 12
/* 13-14 Reserved */
/* Set logging mask Request/Response          */
#define DIAG_LOGMASK_F 0x0F// 15
/* Log packet Request/Response                */
#define DIAG_LOG_F 0x10//16
/* Peek at NV memory Request/Response         */
#define DIAG_NV_PEEK_F 0x11//17
/* Poke at NV memory Request/Response         */
#define DIAG_NV_POKE_F 0x12//18
/* Invalid Command Response                   */
#define DIAG_BAD_CMD_F 0x13//19
/* Invalid parmaeter Response                 */
#define DIAG_BAD_PARM_F 0x14//20
/* Invalid packet length Response             */
#define DIAG_BAD_LEN_F 0x15//21
/* 22-23 Reserved */
/* Packet not allowed in this mode
   ( online vs offline )                      */
#define DIAG_BAD_MODE_F 0x18//24
/* info for TA power and voice graphs         */
#define DIAG_TAGRAPH_F 0x19//25
/* Markov statistics                          */
#define DIAG_MARKOV_F 0x1A//26
/* Reset of Markov statistics                 */
#define DIAG_MARKOV_RESET_F 0x1B//27
/* Return diag version for comparison to
   detect incompatabilities                   */
#define DIAG_DIAG_VER_F 0x1C//28
/* Return a timestamp                         */
#define DIAG_TS_F 0x1D//29
/* Set TA parameters                          */
#define DIAG_TA_PARM_F 0x1E//30
/* Request for msg report                     */
#define DIAG_MSG_F 0x1F//31
/* Handset Emulation -- keypress              */
#define DIAG_HS_KEY_F 0x20//32
/* Handset Emulation -- lock or unlock        */
#define DIAG_HS_LOCK_F 0x21//33
/* Handset Emulation -- display request       */
#define DIAG_HS_SCREEN_F 0x22//34
/* 35 Reserved */
/* Parameter Download                         */
#define DIAG_PARM_SET_F 0x24//36
/* 37 Reserved */
/* Read NV item                               */
#define DIAG_NV_READ_F 0x26//38
/* Write NV item                              */
#define DIAG_NV_WRITE_F 0x27//39
/* 40 Reserved */
/* Mode change request                        */
#define DIAG_CONTROL_F 0x29//41
/* Error record retreival                     */
#define DIAG_ERR_READ_F 0x2A//42
/* Error record clear                         */
#define DIAG_ERR_CLEAR_F 0x2B//43
/* Symbol error rate counter reset            */
#define DIAG_SER_RESET_F 0x2C//44
/* Symbol error rate counter report           */
#define DIAG_SER_REPORT_F 0x2D//45
/* Run a specified test                       */
#define DIAG_TEST_F 0x2E//46
/* Retreive the current dip switch setting    */
#define DIAG_GET_DIPSW_F 0x2F//47
/* Write new dip switch setting               */
#define DIAG_SET_DIPSW_F 0x30//48
/* Start/Stop Vocoder PCM loopback            */
#define DIAG_VOC_PCM_LB_F 0x31//49
/* Start/Stop Vocoder PKT loopback            */
#define DIAG_VOC_PKT_LB_F 0x32//50
/* 51-52 Reserved */
/* Originate a call                           */
#define DIAG_ORIG_F 0x35//53
/* End a call                                 */
#define DIAG_END_F 0x36//54
/* 55-57 Reserved */
/* Switch to downloader                       */
#define DIAG_DLOAD_F 0x3A//58
/* Test Mode Commands and FTM commands        */
#define DIAG_TMOB_F 0x3B//59
/* Test Mode Commands and FTM commands        */
#define DIAG_FTM_CMD_F 0x3B//59
/* 60-62 Reserved */
/* Featurization Removal requested by CMI
#ifdef FEATURE_HWTC
*/
#define DIAG_TEST_STATE_F 0x3D//61
/*
#endif
*/
/* Return the current state of the phone      */
#define DIAG_STATE_F 0x3F//63
/* Return all current sets of pilots          */
#define DIAG_PILOT_SETS_F 0x40//64
/* Send the Service Prog. Code to allow SP    */
#define DIAG_SPC_F 0x41//65
/* Invalid nv_read/write because SP is locked */
#define DIAG_BAD_SPC_MODE_F 0x42//66
/* get parms obsoletes PARM_GET               */
#define DIAG_PARM_GET2_F 0x43//67
/* Serial mode change Request/Response        */
#define DIAG_SERIAL_CHG_F 0x44//68
/* 69 Reserved */
/* Send password to unlock secure operations
   the phone to be in a security state that
   is wasn't - like unlocked.                 */
#define DIAG_PASSWORD_F 0x46//70
/* An operation was attempted which required  */
#define DIAG_BAD_SEC_MODE_F 0x47//71
/* Write Preferred Roaming list to the phone. */
#define DIAG_PR_LIST_WR_F 0x48//72
/* Read Preferred Roaming list from the phone.*/
#define DIAG_PR_LIST_RD_F 0x49//73
/* 74 Reserved */
/* Subssytem dispatcher (extended diag cmd)   */
#define DIAG_SUBSYS_CMD_F 0x4B//75
/* 76-80 Reserved */
/* Asks the phone what it supports            */
#define DIAG_FEATURE_QUERY_F 0x51//81
/* 82 Reserved */
/* Read SMS message out of NV                 */
#define DIAG_SMS_READ_F 0x53// 83
/* Write SMS message into NV                  */
#define DIAG_SMS_WRITE_F 0x54// 84
/* info for Frame Error Rate
   on multiple channels                       */
#define DIAG_SUP_FER_F 0x55// 85
/* Supplemental channel walsh codes           */
#define DIAG_SUP_WALSH_CODES_F 0x56// 86
/* Sets the maximum # supplemental
   channels                                   */
#define DIAG_SET_MAX_SUP_CH_F 0x57// 87
/* get parms including SUPP and MUX2:
   obsoletes PARM_GET and PARM_GET_2          */
#define DIAG_PARM_GET_IS95B_F 0x58// 88
/* Performs an Embedded File System
   (EFS) operation.                           */
#define DIAG_FS_OP_F 0x59// 89
/* AKEY Verification.                         */
#define DIAG_AKEY_VERIFY_F 0x5A// 90
/* Handset emulation - Bitmap screen          */
#define DIAG_BMP_HS_SCREEN_F 0x5B// 91
/* Configure communications                   */
#define DIAG_CONFIG_COMM_F 0x5C// 92
/* Extended logmask for > 32 bits.            */
#define DIAG_EXT_LOGMASK_F 0x5D// 93
/* 94-95 reserved */
/* Static Event reporting.                    */
#define DIAG_EVENT_REPORT_F 0x60// 96
/* Load balancing and more!                   */
#define DIAG_STREAMING_CONFIG_F 0x61// 97
/* Parameter retrieval                        */
#define DIAG_PARM_RETRIEVE_F 0x62  // 98
                                   /* A state/status snapshot of the DMSS.      */
#define DIAG_STATUS_SNAPSHOT_F 0x63// 99
/* 100 obsolete  */
/* Get_property requests                      */
#define DIAG_GET_PROPERTY_F 0x65// 101
/* Put_property requests                      */
#define DIAG_PUT_PROPERTY_F 0x66// 102
/* Get_guid requests                          */
#define DIAG_GET_GUID_F 0x67// 103
/* Invocation of user callbacks               */
#define DIAG_USER_CMD_F 0x68// 104
/* Get permanent properties                   */
#define DIAG_GET_PERM_PROPERTY_F 0x69// 105
/* Put permanent properties                   */
#define DIAG_PUT_PERM_PROPERTY_F 0x6A// 106
/* Permanent user callbacks                   */
#define DIAG_PERM_USER_CMD_F 0x6B// 107
/* GPS Session Control                        */
#define DIAG_GPS_SESS_CTRL_F 0x6C// 108
/* GPS search grid                            */
#define DIAG_GPS_GRID_F 0x6D// 109
/* GPS Statistics                             */
#define DIAG_GPS_STATISTICS_F 0x6E// 110
/* Packet routing for multiple instances of diag */
#define DIAG_ROUTE_F 0x6F// 111
/* IS2000 status                              */
#define DIAG_IS2000_STATUS_F 0x70// 112
/* RLP statistics reset                       */
#define DIAG_RLP_STAT_RESET_F 0x71// 113
/* (S)TDSO statistics reset                   */
#define DIAG_TDSO_STAT_RESET_F 0x72// 114
/* Logging configuration packet               */
#define DIAG_LOG_CONFIG_F 0x73// 115
/* Static Trace Event reporting */
#define DIAG_TRACE_EVENT_REPORT_F 0x74// 116
/* SBI Read */
#define DIAG_SBI_READ_F 0x75// 117
/* SBI Write */
#define DIAG_SBI_WRITE_F 0x76// 118
/* SSD Verify */
#define DIAG_SSD_VERIFY_F 0x77// 119
/* Log on Request */
#define DIAG_LOG_ON_DEMAND_F 0x78// 120
/* Request for extended msg report */
#define DIAG_EXT_MSG_F 0x79// 121
/* ONCRPC diag packet */
#define DIAG_ONCRPC_F 0x7A// 122
/* Diagnostics protocol loopback. */
#define DIAG_PROTOCOL_LOOPBACK_F 0x7B//123
/* Extended build ID text */
#define DIAG_EXT_BUILD_ID_F 0x7C// 124
/* Request for extended msg report */
#define DIAG_EXT_MSG_CONFIG_F 0x7D// 125
/* Extended messages in terse format */
#define DIAG_EXT_MSG_TERSE_F 0x7E// 126
/* Translate terse format message identifier */
#define DIAG_EXT_MSG_TERSE_XLATE_F 0x7F// 127
/* Subssytem dispatcher Version 2 (delayed response capable) */
#define DIAG_SUBSYS_CMD_VER_2_F 0x80// 128
/* Get the event mask */
#define DIAG_EVENT_MASK_GET_F 0x81// 129
/* Set the event mask */
#define DIAG_EVENT_MASK_SET_F 0x82// 130
/* RESERVED CODES: 131-139 */
/* Command Code for Changing Port Settings */
#define DIAG_CHANGE_PORT_SETTINGS 0x8C// 140
/* Country network information for assisted dialing */
#define DIAG_CNTRY_INFO_F 0x8D// 141
/* Send a Supplementary Service Request */
#define DIAG_SUPS_REQ_F 0x8E// 142
/* Originate SMS request for MMS */
#define DIAG_MMS_ORIG_SMS_REQUEST_F 0x8F// 143
/* Change measurement mode*/
#define DIAG_MEAS_MODE_F 0x90// 144
/* Request measurements for HDR channels */
#define DIAG_MEAS_REQ_F 0x91// 145
/* Send Optimized F3 messages */
#define DIAG_QSR_EXT_MSG_TERSE_F 0x92// 146
/* Packet ID for command/responses sent over DCI */
#define DIAG_DCI_CMD_REQ 0x93// 147
/* Packet ID for delayed responses sent over DCI */
#define DIAG_DCI_DELAYED_RSP 0x94// 148
/* Error response code on DCI (only APSS side) */
#define DIAG_BAD_TRANS_F 0x95// 149
/* Error response code for cmomands disallowed by SSM */
#define DIAG_SSM_DISALLOWED_CMD_F 0x96// 150
/* Log on extended Request */
#define DIAG_LOG_ON_DEMAND_EXT_F 0x97// 151
/* Packet ID for extended event/log/F3 pkt */
#define DIAG_CMD_EXT_F 0x98// 152
/*Qshrink4 command code for Qshrink 4 packet*/
#define DIAG_QSR4_EXT_MSG_TERSE_F 0x99// 153
/*DCI command code for dci control packet*/
#define DIAG_DCI_CONTROL_PACKET 0x9A// 154
/*Compressed diag data which is sent out by the DMSS to the host.*/
#define DIAG_COMPRESSED_PKT 0x9B     // 155
#define DIAG_MSG_SMALL_F 0x9C        // 156
#define DIAG_QSH_TRACE_PAYLOAD_F 0x9D// 157
#define DIAG_SECURE_LOG_F 0x9E       // 158

///* Number of packets defined. *///
//#define DIAG_MAX_F                 0x9B // 155

//SCAT header from log_codes.h
#define DIAG_SUBSYS_ID_1X 0x01
#define DIAG_SUBSYS_ID_WCDMA 0x04
#define DIAG_SUBSYS_ID_GSM 0x05
#define DIAG_SUBSYS_ID_UMTS 0x07
#define DIAG_SUBSYS_ID_DTV 0x0A
#define DIAG_SUBSYS_ID_APPS 0x0B
#define DIAG_SUBSYS_ID_LTE 0x0B// Also shared by NR
#define DIAG_SUBSYS_ID_TDSCDMA 0x0D

//LOG CONFIG OPERATIONS
#define LOG_CONFIG_DISABLE_OP 0
#define LOG_CONFIG_RETRIEVE_ID_RANGES_OP 1
#define LOG_CONFIG_RETRIEVE_VALID_MASK_OP 2
#define LOG_CONFIG_SET_MASK_OP 3
#define LOG_CONFIG_GET_LOGMASK_OP 4


typedef enum {
    DIAG_SUBSYS_OEM = 0,    /* Reserved for OEM use */
    DIAG_SUBSYS_ZREX = 1,   /* ZREX */
    DIAG_SUBSYS_SD = 2,     /* System Determination */
    DIAG_SUBSYS_BT = 3,     /* Bluetooth */
    DIAG_SUBSYS_WCDMA = 4,  /* WCDMA */
    DIAG_SUBSYS_HDR = 5,    /* 1xEvDO */
    DIAG_SUBSYS_DIABLO = 6, /* DIABLO */
    DIAG_SUBSYS_TREX = 7,   /* TREX - Off-target testing environments */
    DIAG_SUBSYS_GSM = 8,    /* GSM */
    DIAG_SUBSYS_UMTS = 9,   /* UMTS */
    DIAG_SUBSYS_HWTC = 10,  /* HWTC */
    DIAG_SUBSYS_FTM = 11,   /* Factory Test Mode */
    DIAG_SUBSYS_REX = 12,   /* Rex */
    DIAG_SUBSYS_OS = DIAG_SUBSYS_REX,
    DIAG_SUBSYS_GPS = 13,               /* Global Positioning System */
    DIAG_SUBSYS_WMS = 14,               /* Wireless Messaging Service (WMS, SMS) */
    DIAG_SUBSYS_CM = 15,                /* Call Manager */
    DIAG_SUBSYS_HS = 16,                /* Handset */
    DIAG_SUBSYS_AUDIO_SETTINGS = 17,    /* Audio Settings */
    DIAG_SUBSYS_DIAG_SERV = 18,         /* DIAG Services */
    DIAG_SUBSYS_FS = 19,                /* File System - EFS2 */
    DIAG_SUBSYS_PORT_MAP_SETTINGS = 20, /* Port Map Settings */
    DIAG_SUBSYS_MEDIAPLAYER = 21,       /* QCT Mediaplayer */
    DIAG_SUBSYS_QCAMERA = 22,           /* QCT QCamera */
    DIAG_SUBSYS_MOBIMON = 23,           /* QCT MobiMon */
    DIAG_SUBSYS_GUNIMON = 24,           /* QCT GuniMon */
    DIAG_SUBSYS_LSM = 25,               /* Location Services Manager */
    DIAG_SUBSYS_QCAMCORDER = 26,        /* QCT QCamcorder */
    DIAG_SUBSYS_MUX1X = 27,             /* Multiplexer */
    DIAG_SUBSYS_DATA1X = 28,            /* Data */
    DIAG_SUBSYS_SRCH1X = 29,            /* Searcher */
    DIAG_SUBSYS_CALLP1X = 30,           /* Call Processor */
    DIAG_SUBSYS_APPS = 31,              /* Applications */
    DIAG_SUBSYS_SETTINGS = 32,          /* Settings */
    DIAG_SUBSYS_GSDI = 33,              /* Generic SIM Driver Interface */
    DIAG_SUBSYS_UIMDIAG = DIAG_SUBSYS_GSDI,
    DIAG_SUBSYS_TMC = 34, /* Task Main Controller */
    DIAG_SUBSYS_USB = 35, /* Universal Serial Bus */
    DIAG_SUBSYS_PM = 36,  /* Power Management */
    DIAG_SUBSYS_DEBUG = 37,
    DIAG_SUBSYS_QTV = 38,
    DIAG_SUBSYS_CLKRGM = 39, /* Clock Regime */
    DIAG_SUBSYS_DEVICES = 40,
    DIAG_SUBSYS_WLAN = 41,            /* 802.11 Technology */
    DIAG_SUBSYS_PS_DATA_LOGGING = 42, /* Data Path Logging */
    DIAG_SUBSYS_PS = DIAG_SUBSYS_PS_DATA_LOGGING,
    DIAG_SUBSYS_MFLO = 43, /* MediaFLO */
    DIAG_SUBSYS_DTV = 44,  /* Digital TV */
    DIAG_SUBSYS_RRC = 45,  /* WCDMA Radio Resource Control state */
    DIAG_SUBSYS_PROF = 46, /* Miscellaneous Profiling Related */
    DIAG_SUBSYS_TCXOMGR = 47,
    DIAG_SUBSYS_NV = 48, /* Non Volatile Memory */
    DIAG_SUBSYS_AUTOCONFIG = 49,
    DIAG_SUBSYS_PARAMS = 50, /* Parameters required for debugging subsystems */
    DIAG_SUBSYS_MDDI = 51,   /* Mobile Display Digital Interface */
    DIAG_SUBSYS_DS_ATCOP = 52,
    DIAG_SUBSYS_L4LINUX = 53,              /* L4/Linux */
    DIAG_SUBSYS_MVS = 54,                  /* Multimode Voice Services */
    DIAG_SUBSYS_CNV = 55,                  /* Compact NV */
    DIAG_SUBSYS_APIONE_PROGRAM = 56,       /* apiOne */
    DIAG_SUBSYS_HIT = 57,                  /* Hardware Integration Test */
    DIAG_SUBSYS_DRM = 58,                  /* Digital Rights Management */
    DIAG_SUBSYS_DM = 59,                   /* Device Management */
    DIAG_SUBSYS_FC = 60,                   /* Flow Controller */
    DIAG_SUBSYS_MEMORY = 61,               /* Malloc Manager */
    DIAG_SUBSYS_FS_ALTERNATE = 62,         /* Alternate File System */
    DIAG_SUBSYS_REGRESSION = 63,           /* Regression Test Commands */
    DIAG_SUBSYS_SENSORS = 64,              /* The sensors subsystem */
    DIAG_SUBSYS_FLUTE = 65,                /* FLUTE */
    DIAG_SUBSYS_ANALOG = 66,               /* Analog die subsystem */
    DIAG_SUBSYS_APIONE_PROGRAM_MODEM = 67, /* apiOne Program On Modem Processor */
    DIAG_SUBSYS_LTE = 68,                  /* LTE */
    DIAG_SUBSYS_BREW = 69,                 /* BREW */
    DIAG_SUBSYS_PWRDB = 70,                /* Power Debug Tool */
    DIAG_SUBSYS_CHORD = 71,                /* Chaos Coordinator */
    DIAG_SUBSYS_SEC = 72,                  /* Security */
    DIAG_SUBSYS_TIME = 73,                 /* Time Services */
    DIAG_SUBSYS_Q6_CORE = 74,              /* Q6 core services */
    DIAG_SUBSYS_COREBSP = 75,              /* CoreBSP */
                                           /* Command code allocation:
                                                [0 - 2047]	- HWENGINES
                                                [2048 - 2147]	- MPROC
                                                [2148 - 2247]	- BUSES
                                                [2248 - 2347]	- USB
                                                [2348 - 2447]   - FLASH
                                                [2448 - 3447]   - UART
                                                [3448 - 3547]   - PRODUCTS
                                                [3547 - 65535]	- Reserved
                                            */

    DIAG_SUBSYS_MFLO2 = 76,   /* Media Flow */
                              /* Command code allocation:
                                                [0 - 1023]       - APPs
                                                [1024 - 65535]   - Reserved
                                            */
    DIAG_SUBSYS_ULOG = 77,    /* ULog Services */
    DIAG_SUBSYS_APR = 78,     /* Asynchronous Packet Router (Yu, Andy)*/
    DIAG_SUBSYS_QNP = 79,     /*QNP (Ravinder Are , Arun Harnoor)*/
    DIAG_SUBSYS_STRIDE = 80,  /* Ivailo Petrov */
    DIAG_SUBSYS_OEMDPP = 81,  /* to read/write calibration to DPP partition */
    DIAG_SUBSYS_Q5_CORE = 82, /* Requested by ADSP team */
    DIAG_SUBSYS_USCRIPT = 83, /* core/power team USCRIPT tool */
    DIAG_SUBSYS_NAS = 84,     /* Requested by 3GPP NAS team */
    DIAG_SUBSYS_CMAPI = 85,   /* Requested by CMAPI */
    DIAG_SUBSYS_SSM = 86,
    DIAG_SUBSYS_TDSCDMA = 87, /* Requested by TDSCDMA team */
    DIAG_SUBSYS_SSM_TEST = 88,
    DIAG_SUBSYS_MPOWER = 89, /* Requested by MPOWER team */
    DIAG_SUBSYS_QDSS = 90,   /* For QDSS STM commands */
    DIAG_SUBSYS_CXM = 91,
    DIAG_SUBSYS_GNSS_SOC = 92, /* Secondary GNSS system */
    DIAG_SUBSYS_TTLITE = 93,
    DIAG_SUBSYS_FTM_ANT = 94,
    DIAG_SUBSYS_MLOG = 95,
    DIAG_SUBSYS_LIMITSMGR = 96,
    DIAG_SUBSYS_EFSMONITOR = 97,
    DIAG_SUBSYS_DISPLAY_CALIBRATION = 98,
    DIAG_SUBSYS_VERSION_REPORT = 99,
    DIAG_SUBSYS_DS_IPA = 100,
    DIAG_SUBSYS_SYSTEM_OPERATIONS = 101,
    DIAG_SUBSYS_CNSS_POWER = 102,
    DIAG_SUBSYS_LWIP = 103,
    DIAG_SUBSYS_IMS_QVP_RTP = 104,
    DIAG_SUBSYS_STORAGE = 105,
    DIAG_SUBSYS_WCI2 = 106,
    DIAG_SUBSYS_AOSTLM_TEST = 107,

    DIAG_SUBSYS_LAST,

    /* Subsystem IDs reserved for OEM use */
    DIAG_SUBSYS_RESERVED_OEM_0 = 250,
    DIAG_SUBSYS_RESERVED_OEM_1 = 251,
    DIAG_SUBSYS_RESERVED_OEM_2 = 252,
    DIAG_SUBSYS_RESERVED_OEM_3 = 253,
    DIAG_SUBSYS_RESERVED_OEM_4 = 254,
    DIAG_SUBSYS_LEGACY = 255
} diagpkt_subsys_cmd_enum_type;

//log code 1x 0x1000 + x

typedef enum {
    // SIM
    LOG_UIM_DATA_C = 0x98,                 // 0x1098 RUIM Debug
    LOG_GENERIC_SIM_TOOLKIT_TASK_C = 0x272,// 0x1272 Generic SIM Toolkit Task
    LOG_UIM_DS_DATA_C = 0x4ce,             // 0x14CE UIM DS Data

    // IP
    LOG_DATA_PROTOCOL_LOGGING_C = 0x1eb,                          // 0x11EB Protocol Services Data
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_RM_TX_80_BYTES_C = 0x572,// 0x1572 Network IP Rm Tx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_RM_RX_80_BYTES_C = 0x573,// 0x1573 Network IP Rm Rx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_RM_TX_FULL_C = 0x574,    // 0x1574 Network IP Rm Tx Full
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_RM_RX_FULL_C = 0x575,    // 0x1575 Network IP Rm Rx Full
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_UM_TX_80_BYTES_C = 0x576,// 0x1576 Network IP Um Tx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_UM_RX_80_BYTES_C = 0x577,// 0x1577 Network IP Um Rx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_UM_TX_FULL_C = 0x578,    // 0x1578 Network IP Um Tx Full
    LOG_DATA_PROTOCOL_LOGGING_NETWORK_IP_UM_RX_FULL_C = 0x579,    // 0x1579 Network IP Um Rx Full
    LOG_DATA_PROTOCOL_LOGGING_LINK_RM_TX_80_BYTES_C = 0x57a,      // 0x157A Link Rm Tx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_LINK_RM_RX_80_BYTES_C = 0x57b,      // 0x157B Link Rm Rx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_LINK_RM_TX_FULL_C = 0x57c,          // 0x157C Link Rm Tx Full
    LOG_DATA_PROTOCOL_LOGGING_LINK_RM_RX_FULL_C = 0x57d,          // 0x157D Link Rm Rx Full
    LOG_DATA_PROTOCOL_LOGGING_LINK_UM_TX_80_BYTES_C = 0x57e,      // 0x157E Link Um Tx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_LINK_UM_RX_80_BYTES_C = 0x57f,      // 0x157F Link Um Rx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_LINK_UM_TX_FULL_C = 0x580,          // 0x1580 Link Um Tx Full
    LOG_DATA_PROTOCOL_LOGGING_LINK_UM_RX_FULL_C = 0x581,          // 0x1581 Link Um Rx Full
    LOG_DATA_PROTOCOL_LOGGING_FLOW_RM_TX_80_BYTES_C = 0x582,      // 0x1582 Flow Rm Tx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_FLOW_RM_TX_FULL_C = 0x583,          // 0x1583 Flow Rm Tx Full
    LOG_DATA_PROTOCOL_LOGGING_FLOW_UM_TX_80_BYTES_C = 0x584,      // 0x1584 Flow Um Tx 80 Bytes
    LOG_DATA_PROTOCOL_LOGGING_FLOW_UM_TX_FULL_C = 0x585,          // 0x1585 Flow Um Tx Full

    // IMS
    LOG_IMS_RTP_SN_PAYLOAD = 0x568,     // 0x1568 IMS RTP SN and Payload
    LOG_IMS_RTP_PACKET_LOSS = 0x569,    // 0x1569 IMS RTP Packet Loss
    LOG_IMS_RTCP = 0x56A,               // 0x156A IMS RTCP
    LOG_IMS_SIP_MESSAGE = 0x56E,        // 0x156E IMS SIP Message
    LOG_IMS_VOICE_CALL_STATS = 0x7F2,   // 0x17F2 IMS Voice Call Statistics
    LOG_IMS_VOLTE_SESSION_SETUP = 0x830,// 0x1830 IMS VoLTE Session Setup
    LOG_IMS_VOLTE_SESSION_END = 0x831,  // 0x1831 IMS VoLTE Session End
    LOG_IMS_REGISTRATION = 0x832,       // 0x1832 IMS Registration

    // QMI
    LOG_QMI_LINK_01_RX_MSG_C = 0x38e,      // 0x138E QMI Link 1 RX Message
    LOG_QMI_LINK_01_TX_MSG_C = 0x38f,      // 0x138E QMI Link 1 TX Message
    LOG_QMI_LINK_02_RX_MSG_C = 0x390,      // 0x138E QMI Link 2 RX Message
    LOG_QMI_LINK_02_TX_MSG_C = 0x391,      // 0x138E QMI Link 2 TX Message
    LOG_QMI_LINK_03_RX_MSG_C = 0x392,      // 0x138E QMI Link 3 RX Message
    LOG_QMI_LINK_03_TX_MSG_C = 0x393,      // 0x138E QMI Link 3 TX Message
    LOG_QMI_LINK_04_RX_MSG_C = 0x394,      // 0x138E QMI Link 4 RX Message
    LOG_QMI_LINK_04_TX_MSG_C = 0x395,      // 0x138E QMI Link 4 TX Message
    LOG_QMI_LINK_05_RX_MSG_C = 0x396,      // 0x138E QMI Link 5 RX Message
    LOG_QMI_LINK_05_TX_MSG_C = 0x397,      // 0x138E QMI Link 5 TX Message
    LOG_QMI_LINK_06_RX_MSG_C = 0x398,      // 0x138E QMI Link 6 RX Message
    LOG_QMI_LINK_06_TX_MSG_C = 0x399,      // 0x138E QMI Link 6 TX Message
    LOG_QMI_LINK_07_RX_MSG_C = 0x39a,      // 0x138E QMI Link 7 RX Message
    LOG_QMI_LINK_07_TX_MSG_C = 0x39b,      // 0x138E QMI Link 7 TX Message
    LOG_QMI_LINK_08_RX_MSG_C = 0x39c,      // 0x138E QMI Link 8 RX Message
    LOG_QMI_LINK_08_TX_MSG_C = 0x39d,      // 0x138E QMI Link 8 TX Message
    LOG_QMI_LINK_09_RX_MSG_C = 0x39e,      // 0x138E QMI Link 9 RX Message
    LOG_QMI_LINK_09_TX_MSG_C = 0x39f,      // 0x138E QMI Link 9 TX Message
    LOG_QMI_LINK_10_RX_MSG_C = 0x3a0,      // 0x138E QMI Link 10 RX Message
    LOG_QMI_LINK_10_TX_MSG_C = 0x3a1,      // 0x138E QMI Link 10 TX Message
    LOG_QMI_LINK_11_RX_MSG_C = 0x3a2,      // 0x138E QMI Link 11 RX Message
    LOG_QMI_LINK_11_TX_MSG_C = 0x3a3,      // 0x138E QMI Link 11 TX Message
    LOG_QMI_LINK_12_RX_MSG_C = 0x3a4,      // 0x138E QMI Link 12 RX Message
    LOG_QMI_LINK_12_TX_MSG_C = 0x3a5,      // 0x138E QMI Link 12 TX Message
    LOG_QMI_LINK_13_RX_MSG_C = 0x3a6,      // 0x138E QMI Link 13 RX Message
    LOG_QMI_LINK_13_TX_MSG_C = 0x3a7,      // 0x138E QMI Link 13 TX Message
    LOG_QMI_LINK_14_RX_MSG_C = 0x3a8,      // 0x138E QMI Link 14 RX Message
    LOG_QMI_LINK_14_TX_MSG_C = 0x3a9,      // 0x138E QMI Link 14 TX Message
    LOG_QMI_LINK_15_RX_MSG_C = 0x3aa,      // 0x138E QMI Link 15 RX Message
    LOG_QMI_LINK_15_TX_MSG_C = 0x3ab,      // 0x138E QMI Link 15 TX Message
    LOG_QMI_LINK_16_RX_MSG_C = 0x3ac,      // 0x138E QMI Link 16 RX Message
    LOG_QMI_LINK_16_TX_MSG_C = 0x3ad,      // 0x138E QMI Link 16 TX Message
    LOG_QMI_LINK_17_RX_MSG_C = 0x80b,      // 0x138E QMI Link 17 RX Message
    LOG_QMI_LINK_17_TX_MSG_C = 0x80c,      // 0x138E QMI Link 17 TX Message
    LOG_QMI_LINK_18_RX_MSG_C = 0x80d,      // 0x138E QMI Link 18 RX Message
    LOG_QMI_LINK_18_TX_MSG_C = 0x80e,      // 0x138E QMI Link 18 TX Message
    LOG_QMI_LINK_19_RX_MSG_C = 0x80f,      // 0x138E QMI Link 19 RX Message
    LOG_QMI_LINK_19_TX_MSG_C = 0x810,      // 0x138E QMI Link 19 TX Message
    LOG_QMI_LINK_20_RX_MSG_C = 0x811,      // 0x138E QMI Link 20 RX Message
    LOG_QMI_LINK_20_TX_MSG_C = 0x812,      // 0x138E QMI Link 20 TX Message
    LOG_QMI_LINK_21_RX_MSG_C = 0x92b,      // 0x138E QMI Link 21 RX Message
    LOG_QMI_LINK_21_TX_MSG_C = 0x02c,      // 0x138E QMI Link 21 TX Message
    LOG_QMI_CALL_FLOW_C = 0x4cf,           // 0x14CF QMI Call Flow
    LOG_QMI_SUPPORTED_INTERFACES_C = 0x588,// 0x1588 QMI Supported Interfaces

    // General
    LOG_INTERNAL_CORE_DUMP_C = 0x158,  // 0x1158 Internal - Core Dump
    LOG_SECURE_LOG_PUBLIC_KEY_C = 0xd15// 0x1D15 RSA Public Key
} log_code_1x;


//log code WCDMA 0x4000 + x
typedef enum {
    //L1
    LOG_WCDMA_SEARCH_CELL_RESELECTION_RANK_C = 0x5,// 0x4005 WCDMA Search Cell Reselection Rank
    LOG_WCDMA_PN_SEARCH_EDITION_2_C = 0x179,       // 0x4179 WCDMA PN Search Edition 2
    LOG_WCDMA_FREQ_SCAN_C = 0x1b0,                 // 0x41B0 WCDMA Freq Scan

    //L2
    LOG_WCDMA_RLC_DL_AM_SIGNALING_PDU_C = 0x135,  //0x4135 WCDMA RLC DL AM Signaling PDU
    LOG_WCDMA_RLC_UL_AM_SIGNALING_PDU_C = 0x13c,  // 0x413C WCDMA RLC UL AM Signaling PDU
    LOG_WCDMA_RLC_UL_AM_CONTROL_PDU_LOG_C = 0x145,// 0x4145 WCDMA RLC UL AM Control PDU Log
    LOG_WCDMA_RLC_DL_AM_CONTROL_PDU_LOG_C = 0x146,// 0x4146 WCDMA RLC DL AM Control PDU Log
    LOG_WCDMA_RLC_DL_PDU_CIPHER_PACKET_C = 0x168, // 0x4168 WCDMA RLC DL PDU Cipher Packet
    LOG_WCDMA_RLC_UL_PDU_CIPHER_PACKET_C = 0x169, // 0x4169 WCDMA RLC DL PDU Cipher Packet

    //RRC
    LOG_WCDMA_CELL_ID_C = 0x127,      // 0x4127 WCDMA Cell ID
    LOG_WCDMA_SIB_C = 0x12b,          // 0x412B WCDMA SIB
    LOG_WCDMA_SIGNALING_MSG_C = 0x12f,// 0x412F WCDMA Signaling Messages
} log_code_wcdma;


//log code GSM 0x5000 +x

typedef enum {
    //L1
    LOG_GSM_L1_FCCH_ACQUISITION_C = 0x65,      // 0x5065 GSM L1 FCCH Acquisition
    LOG_GSM_L1_SCH_ACQUISITION_C = 0x66,       // 0x5066 GSM L1 SCH Acquisition
    LOG_GSM_L1_NEW_BURST_METRICS_C = 0x6a,     // 0x506A GSM L1 New Burst Metrics
    LOG_GSM_L1_BURST_METRICS_C = 0x6c,         // 0x506C GSM L1 Burst Metrics
    LOG_GSM_L1_SCELL_BA_LIST_C = 0x71,         // 0x5071 GSM Surround Cell BA List
    LOG_GSM_L1_SCELL_AUX_MEASUREMENTS_C = 0x7a,// 0x507A GSM L1 Serving Auxiliary Measurments
    LOG_GSM_L1_NCELL_AUX_MEASUREMENTS_C = 0x7b,// 0x507B GSM L1 Neighbor Cell Auxiliary Measurments

    //L3
    LOG_GSM_RR_SIGNALING_MESSAGE_C = 0x12f,// 0x512F GSM RR Signaling Message
    LOG_GSM_RR_CELL_INFORMATION_C = 0x134, // 0x5134 GSM RR Cell Information

    //GRPS L3
    LOG_GPRS_RR_PACKET_SI_1_C = 0x1fd,              // 0x51FD GPRS RR Packet System Information 1
    LOG_GPRS_RR_PACKET_SI_2_C = 0x1fe,              // 0x51FE GPRS RR Packet System Information 2
    LOG_GPRS_RR_PACKET_SI_3_C = 0x1ff,              // 0x51FF GPRS RR Packet System Information 3
    LOG_GPRS_MAC_SIGNALING_MESSACE_C = 0x226,       // 0x5226 GPRS MAC Signaling Message
    LOG_GPRS_SM_GMM_OTA_SIGNALING_MESSAGE_C = 0x230,// 0x5230 GPRS SM/GMM OTA Signaling Message

    //DSDS L1
    LOG_GSM_DSDS_L1_FCCH_ACQUISITION_C = 0xa65,      // 0x5A65 GSM DSDS L1 FCCH Acquisition
    LOG_GSM_DSDS_L1_SCH_ACQUISITION_C = 0xa66,       // 0x5A66 GSM DSDS L1 SCH Acquisition
    LOG_GSM_DSDS_L1_BURST_METRICS_C = 0xa6c,         // 0x5A6C GSM DSDS L1 Burst Metrics
    LOG_GSM_DSDS_L1_SCELL_BA_LIST_C = 0xa71,         // 0x5A71 GSM DSDS Surround Cell BA List
    LOG_GSM_DSDS_L1_SCELL_AUX_MEASUREMENTS_C = 0xa7a,// 0x5A7A GSM DSDS L1 Serving Auxiliary Measurments
    LOG_GSM_DSDS_L1_NCELL_AUX_MEASUREMENTS_C = 0xa7b,// 0x5A7B GSM DSDS L1 Neighbor Cell Auxiliary Measurments

    //DSDS L3
    LOG_GSM_DSDS_RR_SIGNALING_MESSAGE_C = 0xb2f,// 0x5B2F GSM DSDS RR Signaling Message
    LOG_GSM_DSDS_RR_CELL_INFORMATION_C = 0xb34, // 0x5B34 GSM DSDS RR Cell Information

    //GRPS DSDS L3
    LOG_GPRS_DSDS_RR_PACKET_SI_1_C = 0xbfd,// 0x5BFD GPRS DSDS RR Packet System Information 1
    LOG_GPRS_DSDS_RR_PACKET_SI_2_C = 0xbfe,// 0x5BFE GPRS DSDS RR Packet System Information 2
    LOG_GPRS_DSDS_RR_PACKET_SI_3_C = 0xbff,// 0x5BFF GPRS DSDS RR Packet System Information 3
} log_code_gsm;


//UMTS 0x7000 + x
typedef enum {
    LOG_UMTS_NAS_OTA_MESSAGE_LOG_PACKET_C = 0x13a,// 0x713A UMTS UE OTA
    LOG_UMTS_DSDS_NAS_SIGNALING_MESSAGE = 0xb3a,  // 0x7B3A UMTS DSDS NAS Signaling Messages
} log_code_umts;


//log code LTE 0xB000 + x
typedef enum {
    //ML1
    LOG_LTE_ML1_MAC_RAR_MSG1_REPORT = 0x167,                          // 0xB167 LTE MAC RAR (Msg1) Report
    LOG_LTE_ML1_MAC_RAR_MSG2_REPORT = 0x168,                          // 0xB168 LTE MAC RAR (Msg2) Report
    LOG_LTE_ML1_MAC_UE_IDENTIFICATION_MESSAGE_MSG3_REPORT = 0x169,    // 0xB169 LTE MAC RAR (Msg3) Report
    LOG_LTE_ML1_MAC_CONTENTION_RESOLUTION_MESSAGE_MSG4_REPORT = 0x16A,// 0xB16A LTE MAC RAR (Msg4) Report
    LOG_LTE_ML1_CONNECTED_MODE_INTRA_FREQ_MEAS = 0x179,               // 0xB179 LTE ML1 Connected Mode LTE Intra-Freq Measurements
    LOG_LTE_ML1_SERVING_CELL_MEAS_AND_EVAL = 0x17f,                   // 0xB17F LTE ML1 Serving Cell Meas and Eval
    LOG_LTE_ML1_NEIGHBOR_MEASUREMENTS = 0x180,                        // 0xB180 LTE ML1 Neighbor Measurements
    LOG_LTE_ML1_INTRA_FREQ_CELL_RESELECTION = 0x181,                  // 0xB181 LTE ML1 Intra Frequency Cell Reselection
    LOG_LTE_ML1_NEIGHBOR_CELL_MEAS_REQ_RESPONSE = 0x192,              // B192 LTE ML1 Neighbor Cell Meas Request/Response
    LOG_LTE_ML1_SERVING_CELL_MEAS_RESPONSE = 0x193,                   // 0xB193 LTE ML1 Serving Cell Meas Response
    LOG_LTE_ML1_SEARCH_REQ_RESPONSE = 0x194,                          // 0xB194 LTE ML1 Search Request/Response
    LOG_LTE_ML1_CONNECTED_MODE_NEIGHBOR_MEAS_REQ_RESPONSE = 0x195,    // 0xB195 LTE ML1 Connected Neighbor Meas Request/Response
    LOG_LTE_ML1_SERVING_CELL_INFO = 0x197,                            //0xB197 LTE ML1 Serving Cell Information


    //MAC
    LOG_LTE_MAC_RACH_TRIGGER = 0x61,      // 0xB061 LTE MAC RACH Trigger
    LOG_LTE_MAC_RACH_RESPONSE = 0x62,     // 0xB062 LTE MAC Rach Attempt
    LOG_LTE_MAC_DL_TRANSPORT_BLOCK = 0x63,// 0xB063 LTE MAC DL Transport Block
    LOG_LTE_MAC_UL_TRANSPORT_BLOCK = 0x64,// 0xB064 LTE MAC UL Transport Block

    //PDCP
    LOG_LTE_PDCP_DL_CONFIG = 0xa0,                // 0xB0A0 LTE PDCP DL Config
    LOG_LTE_PDCP_UL_CONFIG = 0xb0,                // 0xB0B0 LTE PDCP UL Config
    LOG_LTE_PDCP_DL_DATA_PDU = 0xa1,              // 0xB0A1 LTE PDCP DL Data PDU
    LOG_LTE_PDCP_UL_DATA_PDU = 0xb1,              // 0xB0B1 LTE PDCP UL Data PDU
    LOG_LTE_PDCP_DL_CONTROL_PDU = 0xa2,           // 0xB0A2 LTE PDCP DL Ctrl PDU
    LOG_LTE_PDCP_UL_CONTROL_PDU = 0xb2,           // 0xB0B2 LTE PDCP UL Ctrl PDU
    LOG_LTE_PDCP_DL_CIPHER_DATA_PDU = 0xa3,       // 0xB0A3 LTE PDCP DL Cipher Data PDU
    LOG_LTE_PDCP_UL_CIPHER_DATA_PDU = 0xb3,       // 0xB0B3 LTE PDCP UL Cipher Data PDU
    LOG_LTE_PDCP_DL_SRB_INTEGRITY_DATA_PDU = 0xa5,// 0xB0A5 LTE PDCP DL SRB Integrity Data PDU
    LOG_LTE_PDCP_UL_SRB_INTEGRITY_DATA_PDU = 0xb5,// 0xB0B5 LTE PDCP UL SRB Integrity Data PDU

    //RRC
    LOG_LTE_RRC_OTA_MESSAGE = 0xc0,        // 0xB0C0 LTE RRC OTA Packet
    LOG_LTE_RRC_MIB_MESSAGE = 0xc1,        // 0xB0C1 LTE RRC MIB Message Log Packet
    LOG_LTE_RRC_SERVING_CELL_INFO = 0xc2,  // 0xB0C2 LTE RRC Serving Cell Info Log Pkt
    LOG_LTE_RRC_SUPPORTED_CA_COMBOS = 0xcd,// 0xB0CD LTE RRC Supported CA Combos

    //NAS
    LOG_LTE_NAS_ESM_SEC_OTA_INCOMING_MESSAGE = 0xe0,  // 0xB0E0 LTE NAS EMM Security Protected Incoming Msg
    LOG_LTE_NAS_ESM_SEC_OTA_OUTGOING_MESSAGE = 0xe1,  // 0xB0E1 LTE NAS EMM Security Protected Outgoing Msg
    LOG_LTE_NAS_ESM_PLAIN_OTA_INCOMING_MESSAGE = 0xe2,// 0xB0E2 LTE NAS EMM Plain OTA Incoming Message
    LOG_LTE_NAS_ESM_PLAIN_OTA_OUTGOING_MESSAGE = 0xe3,// 0xB0E3 LTE NAS EMM Plain OTA Outgoing Message
    LOG_LTE_NAS_EMM_SEC_OTA_INCOMING_MESSAGE = 0xea,  // 0xB0EA LTE NAS EMM Security Protected Incoming Msg
    LOG_LTE_NAS_EMM_SEC_OTA_OUTGOING_MESSAGE = 0xeb,  // 0xB0EB LTE NAS EMM Security Protected Outgoing Msg
    LOG_LTE_NAS_EMM_PLAIN_OTA_INCOMING_MESSAGE = 0xec,// 0xB0EC LTE NAS EMM Plain OTA Incoming Message
    LOG_LTE_NAS_EMM_PLAIN_OTA_OUTGOING_MESSAGE = 0xed,// 0xB0ED LTE NAS EMM Plain OTA Outgoing Message

} log_code_LTE;


//log code 5gnr
typedef enum {
    // Management Layer 1
    LOG_5GNR_ML1_MEAS_DATABASE_UPDATE = 0x97F,// 0xB97F NR ML1 Measurement Database Update

    //MAC
    LOG_5GNR_MAC_RACH_ATTEMPT = 0x88A,// 0xB88A NR MAC RACH Attempt

    // RRC
    LOG_5GNR_RRC_OTA_MESSAGE = 0x821,        // 0xB821 NR RRC OTA
    LOG_5GNR_RRC_MIB_INFO = 0x822,           // 0xB822 NR RRC MIB Info
    LOG_5GNR_RRC_SERVING_CELL_INFO = 0x823,  // 0xB823 NR RRC Serving Cell Info
    LOG_5GNR_RRC_CONFIGURATION_INFO = 0x825, // 0xB825 NR RRC Configuration Info
    LOG_5GNR_RRC_SUPPORTED_CA_COMBOS = 0x826,// 0xB826 NR RRC Supported CA Combinations

    // NAS
    LOG_5GNR_NAS_5GSM_PLAIN_OTA_INCOMING_MESSAGE = 0x800, // NR NAS 5GSM Plain OTA Incoming Message
    LOG_5GNR_NAS_5GSM_PLAIN_OTA_OUTGOING_MESSAGE = 0x801, // NR NAS 5GSM Plain OTA Outgoing Message
    LOG_5GNR_NAS_5GSM_SEC_OTA_INCOMING_MESSAGE = 0x808,   // NR NAS 5GMM Security Protected OTA Incoming Message
    LOG_5GNR_NAS_5GSM_SEC_OTA_OUTGOING_MESSAGE = 0x809,   // NR NAS 5GMM Security Protected OTA Outgoing Message
    LOG_5GNR_NAS_5GMM_PLAIN_OTA_INCOMING_MESSAGE = 0x80A, // NR NAS 5GMM Plain OTA Incoming Message
    LOG_5GNR_NAS_5GMM_PLAIN_OTA_OUTGOING_MESSAGE = 0x80B, // NR NAS 5GMM Plain OTA Outgoing Message
    LOG_5GNR_NAS_5GMM_PLAIN_OTA_CONTAINER_MESSAGE = 0x814,// NR NAS 5GMM Plain OTA Container Message
    LOG_5GNR_NAS_5GMM_STATE = 0x80C,                      // NR NAS 5GMM State - According to MobileInsight
} log_code_5GNR;


#ifdef __cplusplus
extern "C" {
#endif

/**
 * Вычисляет количество байт, необходимое для хранения указанного числа бит.
 */
static inline size_t bytes_reqd_for_bit(size_t bit) {
    return (bit + 7) / 8;
}

/**
 * Создаёт пакет DIAG_LOG_CONFIG_F для установки маски логирования (операция 3).
 *
 * @param equip_id       Идентификатор подсистемы (например, DIAG_SUBSYS_ID_LTE).
 * @param last_item      Максимальный номер элемента лога (включительно).
 * @param bits           Массив номеров битов (элементов лога), которые нужно установить.
 * @param bit_count      Количество элементов в массиве bits.
 * @param out_len        Указатель для сохранения длины созданного пакета.
 *
 * @return Указатель на буфер с пакетом. Вызывающий должен освободить его с помощью free().
 *         В случае ошибки возвращает NULL.
 */
uint8_t *create_log_config_set_mask(uint32_t equip_id, uint32_t last_item,
                                    const uint32_t *bits, size_t bit_count,
                                    size_t *out_len);

/**
 * Создаёт пакет DIAG_EXT_MSG_CONFIG_F для настройки расширенных сообщений.
 *
 * @param first_ssid     Первый идентификатор подсистемы в диапазоне.
 * @param last_ssid      Последний идентификатор подсистемы в диапазоне.
 * @param masks          Массив структур subsys_log_level_pair, задающих уровень логирования.
 * @param mask_count     Количество элементов в masks.
 * @param out_len        Указатель для сохранения длины созданного пакета.
 *
 * @return Указатель на буфер с пакетом. Вызывающий должен освободить его с помощью free().
 *         В случае ошибки возвращает NULL.
 */
typedef struct {
    uint16_t subsys_id;// Идентификатор подсистемы
    uint32_t log_level;// Уровень логирования
} subsys_log_level_pair;

uint8_t *create_extended_message_config_set_mask(uint16_t first_ssid, uint16_t last_ssid,
                                                 const subsys_log_level_pair *masks, size_t mask_count,
                                                 size_t *out_len);

// Макросы для удобного вызова функций с фиксированным last_item (как в Python)
#define log_mask_empty_1x() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_1X, 0x0fff, NULL, 0, &(size_t) {0})

#define log_mask_empty_wcdma() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_WCDMA, 0x0ff7, NULL, 0, &(size_t) {0})

#define log_mask_empty_gsm() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_GSM, 0x0ff7, NULL, 0, &(size_t) {0})

#define log_mask_empty_umts() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_UMTS, 0x0b5e, NULL, 0, &(size_t) {0})

#define log_mask_empty_dtv() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_DTV, 0x0392, NULL, 0, &(size_t) {0})

#define log_mask_empty_lte() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_LTE, 0x0209, NULL, 0, &(size_t) {0})

#define log_mask_empty_nr() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_LTE, 0x09ff, NULL, 0, &(size_t) {0})

#define log_mask_empty_tdscdma() \
    create_log_config_set_mask(DIAG_SUBSYS_ID_TDSCDMA, 0x0207, NULL, 0, &(size_t) {0})

// Функции, соответствующие log_mask_scat_* из Python (с параметром layers)
// Вместо списка строк используется битовая маска (можно определить флаги).
// Здесь приведены примеры для 1x и LTE; остальные реализуются аналогично.

// Флаги для слоёв (пример для 1x)
#define LOG_LAYER_1X_IP (1 << 0)
#define LOG_LAYER_1X_QMI (1 << 1)

uint8_t *log_mask_scat_1x(uint32_t num_max_items, uint32_t layer_flags, size_t *out_len);

// Флаги для LTE/NR
#define LOG_LAYER_LTE_MAC (1 << 0)
#define LOG_LAYER_LTE_PDCP (1 << 1)
#define LOG_LAYER_LTE_RRC (1 << 2)
#define LOG_LAYER_LTE_NAS (1 << 3)

uint8_t *log_mask_scat_lte(uint32_t num_max_items, uint32_t layer_flags, size_t *out_len);
uint8_t *log_mask_scat_nr(uint32_t num_max_items, uint32_t layer_flags, size_t *out_len);

#ifdef __cplusplus
}
#endif

/*!
@endcond
*/
#endif /* DIAGCMD_H */