#pragma once
#ifndef DIAG_DEFS_H
#define DIAG_DEFS_H

#include "diagcmd.h"
#include <stdio.h>
/* A small static map for command id -> name (expand as needed) */
typedef struct {
    int id;
    const char *name;
} diag_cmd_name_t;

//lsm_defs
#define MSG_MASKS_TYPE 0x00000001
#define LOG_MASKS_TYPE 0x00000002
#define EVENT_MASKS_TYPE 0x00000004
#define PKT_TYPE 0x00000008
#define DEINIT_TYPE 0x00000010
#define USER_SPACE_DATA_TYPE 0x00000020
#define DCI_DATA_TYPE 0x00000040
#define CALLBACK_DATA_TYPE 0x00000080
#define DCI_LOG_MASKS_TYPE 0x00000100
#define DCI_EVENT_MASKS_TYPE 0x00000200
#define DCI_PKT_TYPE 0x00000400

#define USB_MODE 1
#define MEMORY_DEVICE_MODE 2
#define NO_LOGGING_MODE 3
#define UART_MODE 4
#define SOCKET_MODE 5
#define CALLBACK_MODE 6

#define MAX_NUM_FILES_ON_DEVICE 2000 /* If user wants to stop logging on SD after reaching a max file limit */
#define CONTROL_CHAR 0x7E
#define FILE_NAME_LEN 100
#define NUM_PROC 10
/* Token to identify MDM log */
#define MDM_TOKEN -1
/* Token to identify QSC log */
#define QSC_TOKEN -5
#define MSM 0
#define MDM 1
#define QSC 2

#define MODE_NONREALTIME 0
#define MODE_REALTIME 1
#define MODE_UNKNOWN 2

#define DIAG_PROC_DCI 1
#define DIAG_PROC_MEMORY_DEVICE 2

//lsm_dci
/* List of processors - used to query health status */
#define DIAG_ALL_PROC -1
#define DIAG_MODEM_PROC 0
#define DIAG_LPASS_PROC 1
#define DIAG_WCNSS_PROC 2
#define DIAG_APPS_PROC 3

/* This is a bit mask used for peripheral list. */
typedef uint16_t diag_dci_peripherals;
#define DIAG_CON_APSS (0x0001)  /* Bit mask for APSS */
#define DIAG_CON_MPSS (0x0002)  /* Bit mask for MPSS */
#define DIAG_CON_LPASS (0x0004) /* Bit mask for LPASS */
#define DIAG_CON_WCNSS (0x0008) /* Bit mask for WCNSS */

/*
 * The status bit masks when received in a signal handler are to be
 * used in conjunction with the peripheral list bit mask to determine the
 * status for a peripheral. For instance, 0x00010002 would denote an open
 * status on the MPSS
 */
#define DIAG_STATUS_OPEN (0x00010000)   /* Bit mask for DCI channel open status   */
#define DIAG_STATUS_CLOSED (0x00020000) /* Bit mask for DCI channel closed status */

#define ENABLE 1
#define DISABLE 0
#define IN_BUF_SIZE 16384

#define DIAG_INVALID_SIGNAL 0

#define DIAG_PROC_MSM 0
#define DIAG_PROC_MDM 1

//
//enum {
//    DIAG_DCI_NO_ERROR = 1001,	/* No error */
//    DIAG_DCI_NO_REG,		/* Could not register */
//    DIAG_DCI_NO_MEM,		/* Failed memory allocation */
//    DIAG_DCI_NOT_SUPPORTED,		/* This particular client is not supported */
//    DIAG_DCI_HUGE_PACKET,		/* Request/Response Packet too huge */
//    DIAG_DCI_SEND_DATA_FAIL,	/* writing to kernel or remote peripheral fails */
//    DIAG_DCI_ERR_DEREG,		/* Error while de-registering */
//    DIAG_DCI_PARAM_FAIL,		/* Incorrect Parameter */
//    DIAG_DCI_DUP_CLIENT		/* Client already exists for this proc */
//} diag_dci_error_type;


#define LOG_WCDMA_NEIGHBOR_SET (0x4111)
#define LOG_LTE_SERV_CELL_RESULT (0xB193)
#define LOG_LTE_NEIGHBOR_MEAS (0xB192)
static const diag_cmd_name_t diag_cmd_map[] = {
        {DIAG_ESN_F, "DIAG_ESN_F"},
        {DIAG_STATUS_F, "DIAG_STATUS_F"},
        {DIAG_LOGMASK_F, "DIAG_LOGMASK_F"},
        {DIAG_LOG_F, "DIAG_LOG_F"},
        {DIAG_NV_PEEK_F, "DIAG_NV_PEEK_F"},
        {DIAG_NV_POKE_F, "DIAG_NV_POKE_F"},
        {DIAG_BAD_CMD_F, "DIAG_BAD_CMD_F"},
        {DIAG_BAD_PARM_F, "DIAG_BAD_PARM_F"},
        {DIAG_BAD_LEN_F, "DIAG_BAD_LEN_F"},
        {DIAG_BAD_MODE_F, "DIAG_BAD_MODE_F"},
        {DIAG_TAGRAPH_F, "DIAG_TAGRAPH_F"},
        {DIAG_MARKOV_F, "DIAG_MARKOV_F"},
        {DIAG_MARKOV_RESET_F, "DIAG_MARKOV_RESET_F"},
        {DIAG_DIAG_VER_F, "DIAG_DIAG_VER_F"},
        {DIAG_TS_F, "DIAG_TS_F"},
        {DIAG_TA_PARM_F, "DIAG_TA_PARM_F"},
        {DIAG_MSG_F, "DIAG_MSG_F"},
        {DIAG_HS_KEY_F, "DIAG_HS_KEY_F"},
        {DIAG_HS_LOCK_F, "DIAG_HS_LOCK_F"},
        {DIAG_HS_SCREEN_F, "DIAG_HS_SCREEN_F"},
        {DIAG_PARM_SET_F, "DIAG_PARM_SET_F"},
        {DIAG_NV_READ_F, "DIAG_NV_READ_F"},
        {DIAG_NV_WRITE_F, "DIAG_NV_WRITE_F"},
        {DIAG_CONTROL_F, "DIAG_CONTROL_F"},
        {DIAG_ERR_READ_F, "DIAG_ERR_READ_F"},
        {DIAG_ERR_CLEAR_F, "DIAG_ERR_CLEAR_F"},
        {DIAG_SER_RESET_F, "DIAG_SER_RESET_F"},
        {DIAG_SER_REPORT_F, "DIAG_SER_REPORT_F"},
        {DIAG_TEST_F, "DIAG_TEST_F"},
        {DIAG_GET_DIPSW_F, "DIAG_GET_DIPSW_F"},
        {DIAG_SET_DIPSW_F, "DIAG_SET_DIPSW_F"},
        {DIAG_VOC_PCM_LB_F, "DIAG_VOC_PCM_LB_F"},// Start/Stop Vocoder PCM loopback
        {DIAG_VOC_PKT_LB_F, "DIAG_VOC_PKT_LB_F"},// Start/Stop Vocoder PKT loopback
        {DIAG_ORIG_F, "DIAG_ORIG_F"},            // originate call
        {DIAG_END_F, "DIAG_END_F"},              //end call
        {DIAG_DLOAD_F, "DIAG_DLOAD_F"},          // switch to downloader
        {DIAG_TMOB_F, "DIAG_TMOB_F"},            // Test Mode Commands and FTM commands
        {DIAG_FTM_CMD_F, "DIAG_FTM_CMD_F"},      // Test Mode Commands and FTM commands
        {DIAG_TEST_STATE_F, "DIAG_TEST_STATE_F"},
        {DIAG_STATE_F, "DIAG_STATE_F"},                              //Return the current state of the phone
        {DIAG_PILOT_SETS_F, "DIAG_PILOT_SETS_F"},                    //Return the pilot sets of the phone
        {DIAG_SPC_F, "DIAG_SPC_F"},                                  //Send the Service Prog. Code to allow SP
        {DIAG_BAD_SPC_MODE_F, "DIAG_BAD_SPC_MODE_F"},                // Invalid nv_read/write because SP is locked
        {DIAG_PARM_GET2_F, "DIAG_PARM_GET2_F"},                      // get parms obsoletes PARM_GET
        {DIAG_SERIAL_CHG_F, "DIAG_SERIAL_CHG_F"},                    // Serial mode change Request/Response
        {DIAG_PASSWORD_F, "DIAG_PASSWORD_F"},                        // Send password to unlock secure operations the phone to be in a security state that is wasn't - like unlocked.
        {DIAG_BAD_SEC_MODE_F, "DIAG_BAD_SEC_MODE_F"},                //An operation was attempted which required
        {DIAG_PR_LIST_WR_F, "DIAG_PR_LIST_WR_F"},                    // Write Preferred Roaming list to the phone
        {DIAG_PR_LIST_RD_F, "DIAG_PR_LIST_RD_F"},                    // Read Preferred Roaming list from the phone
        {DIAG_SUBSYS_CMD_F, "DIAG_SUBSYS_CMD_F"},                    // Subssytem dispatcher (extended diag cmd)
        {DIAG_FEATURE_QUERY_F, "DIAG_FEATURE_QUERY_F"},              // Asks the phone what it supports
        {DIAG_SMS_READ_F, "DIAG_SMS_READ_F"},                        // Read SMS message out of NV
        {DIAG_SMS_WRITE_F, "DIAG_SMS_WRITE_F"},                      // Write SMS message into NV
        {DIAG_SUP_FER_F, "DIAG_SUP_FER_F"},                          //info for Frame Error Rate on multiple channels
        {DIAG_SUP_WALSH_CODES_F, "DIAG_SUP_WALSH_CODES_F"},          //Supplemental channel walsh codes
        {DIAG_SET_MAX_SUP_CH_F, "DIAG_SET_MAX_SUP_CH_F"},            //Set the maximum number of supplemental channels
        {DIAG_PARM_GET_IS95B_F, "DIAG_PARM_GET_IS95B_F"},            // get parms including SUPP and MUX2: obsoletes PARM_GET and PARM_GET_2
        {DIAG_FS_OP_F, "DIAG_FS_OP_F"},                              // Performs an Embedded File System (EFS) operation.
        {DIAG_AKEY_VERIFY_F, "DIAG_AKEY_VERIFY_F"},                  // AKEY Verification
        {DIAG_BMP_HS_SCREEN_F, "DIAG_BMP_HS_SCREEN_F"},              // Handset emulation - Bitmap screen
        {DIAG_CONFIG_COMM_F, "DIAG_CONFIG_COMM_F"},                  //Configure communications
        {DIAG_EXT_LOGMASK_F, "DIAG_EXT_LOGMASK_F"},                  //Extended LogMask for more then 32 bits.
        {DIAG_EVENT_REPORT_F, "DIAG_EVENT_REPORT_F"},                //Static Event reporting.
        {DIAG_STREAMING_CONFIG_F, "DIAG_STREAMING_CONFIG_F"},        // Load balancing and more!
        {DIAG_PARM_RETRIEVE_F, "DIAG_PARM_RETRIEVE_F"},              //  Parameter retrieval
        {DIAG_STATUS_SNAPSHOT_F, "DIAG_STATUS_SNAPSHOT_F"},          // A state/status snapshot of the DMSS.
        {DIAG_GET_PROPERTY_F, "DIAG_GET_PROPERTY_F"},                // Get_property requests
        {DIAG_PUT_PROPERTY_F, "DIAG_PUT_PROPERTY_F"},                // Put_property requests
        {DIAG_GET_GUID_F, "DIAG_GET_GUID_F"},                        // Get_guid requests
        {DIAG_USER_CMD_F, "DIAG_USER_CMD_F"},                        //Invocation of user callbacks
        {DIAG_GET_PERM_PROPERTY_F, "DIAG_GET_PERM_PROPERTY_F"},      // Get permanent properties
        {DIAG_PUT_PERM_PROPERTY_F, "DIAG_PUT_PERM_PROPERTY_F"},      //  Put permanent properties
        {DIAG_PERM_USER_CMD_F, "DIAG_PERM_USER_CMD_F"},              // Permanent user callbacks
        {DIAG_GPS_SESS_CTRL_F, "DIAG_GPS_SESS_CTRL_F"},              // GPS Session Control
        {DIAG_GPS_GRID_F, "DIAG_GPS_GRID_F"},                        //GPS search grid
        {DIAG_GPS_STATISTICS_F, "DIAG_GPS_STATISTICS_F"},            // GPS statistics
        {DIAG_ROUTE_F, "DIAG_ROUTE_F"},                              // Packet routing for multiple instances of diag
        {DIAG_IS2000_STATUS_F, "DIAG_IS2000_STATUS_F"},              // IS2000 status information
        {DIAG_RLP_STAT_RESET_F, "DIAG_RLP_STAT_RESET_F"},            //RLP statistics reset
        {DIAG_TDSO_STAT_RESET_F, "DIAG_TDSO_STAT_RESET_F"},          // TDSO statistics reset
        {DIAG_LOG_CONFIG_F, "DIAG_LOG_CONFIG_F"},                    // Logging configuration packet
        {DIAG_TRACE_EVENT_REPORT_F, "DIAG_TRACE_EVENT_REPORT_F"},    // Static Trace Event reporting
        {DIAG_SBI_READ_F, "DIAG_SBI_READ_F"},                        // SBI read
        {DIAG_SBI_WRITE_F, "DIAG_SBI_WRITE_F"},                      // SBI write
        {DIAG_SSD_VERIFY_F, "DIAG_SSD_VERIFY_F"},                    // SSD Verify
        {DIAG_LOG_ON_DEMAND_F, "DIAG_LOG_ON_DEMAND_F"},              // Log on Request
        {DIAG_EXT_MSG_F, "DIAG_EXT_MSG_F"},                          // Request for extended msg report
        {DIAG_ONCRPC_F, "DIAG_ONCRPC_F"},                            // Oncrpc commands
        {DIAG_PROTOCOL_LOOPBACK_F, "DIAG_PROTOCOL_LOOPBACK_F"},      // Diagnostics protocol loopback
        {DIAG_EXT_BUILD_ID_F, "DIAG_EXT_BUILD_ID_F"},                // Extended Build ID text
        {DIAG_EXT_MSG_CONFIG_F, "DIAG_EXT_MSG_CONFIG_F"},            // Extended Msg Config
        {DIAG_EXT_MSG_TERSE_F, "DIAG_EXT_MSG_TERSE_F"},              // Extended Msg terse format
        {DIAG_EXT_MSG_TERSE_XLATE_F, "DIAG_EXT_MSG_TERSE_XLATE_F"},  // Extended Msg terse format with translation
        {DIAG_SUBSYS_CMD_VER_2_F, "DIAG_SUBSYS_CMD_VER_2_F"},        // Subssytem dispatcher Version 2 (delayed response capable)
        {DIAG_EVENT_MASK_GET_F, "DIAG_EVENT_MASK_GET_F"},            // Get event mask
        {DIAG_EVENT_MASK_SET_F, "DIAG_EVENT_MASK_SET_F"},            // Set event mask
        {DIAG_CHANGE_PORT_SETTINGS, "DIAG_CHANGE_PORT_SETTINGS"},    //Command Code for Changing Port Settings
        {DIAG_CNTRY_INFO_F, "DIAG_CNTRY_INFO_F"},                    //  Country network information for assisted dialing
        {DIAG_SUPS_REQ_F, "DIAG_SUPS_REQ_F"},                        // Supplementary service request
        {DIAG_MMS_ORIG_SMS_REQUEST_F, "DIAG_MMS_ORIG_SMS_REQUEST_F"},// Originate an SMS from the phone
        {DIAG_MEAS_MODE_F, "DIAG_MEAS_MODE_F"},                      // Set the phone's measurement mode
        {DIAG_MEAS_REQ_F, "DIAG_MEAS_REQ_F"},                        // Request a measurement for HDR channels
        {DIAG_QSR_EXT_MSG_TERSE_F, "DIAG_QSR_EXT_MSG_TERSE_F"},      //Send Optimized F3 messages
        {DIAG_DCI_CMD_REQ, "DIAG_DCI_CMD_REQ"},                      //  Packet ID for command/responses sent over DCI
        {DIAG_DCI_DELAYED_RSP, "DIAG_DCI_DELAYED_RSP"},              // Packet ID for delayed responses sent over DCI
        {DIAG_BAD_TRANS_F, "DIAG_BAD_TRANS_F"},                      // Error response code on DCI (only APSS side)
        {DIAG_SSM_DISALLOWED_CMD_F, "DIAG_SSM_DISALLOWED_CMD_F"},    //Error response code for cmomands disallowed by SSM
        {DIAG_LOG_ON_DEMAND_EXT_F, "DIAG_LOG_ON_DEMAND_EXT_F"},      //  Log on extended Request
        {DIAG_CMD_EXT_F, "DIAG_CMD_EXT_F"},                          //  Packet ID for extended event/log/F3 pkt
        {DIAG_QSR4_EXT_MSG_TERSE_F, "DIAG_QSR4_EXT_MSG_TERSE_F"},    //Qshrink4 command code for Qshrink 4 packet
        {DIAG_DCI_CONTROL_PACKET, "DIAG_DCI_CONTROL_PACKET"},        // DCI command code for dci control packet
        {DIAG_COMPRESSED_PKT, "DIAG_COMPRESSED_PKT"},                //Compressed diag data which is sent out by the DMSS to the host.
        {-1, NULL}};

/* Subsystem ids (from diagcmd.h enum diagpkt_subsys_cmd_enum_type)
   This list includes the most useful/commonly-seen subsystem ids.
   Add more entries if needed (see referenced header in repos). */
typedef struct {
    int id;
    const char *name;
} diag_subsys_name_t;

static const diag_subsys_name_t diag_subsys_map[] = {
        {DIAG_SUBSYS_OEM, "DIAG_SUBSYS_OEM"},
        {DIAG_SUBSYS_ZREX, "DIAG_SUBSYS_ZREX"},
        {DIAG_SUBSYS_SD, "DIAG_SUBSYS_SD"},
        {DIAG_SUBSYS_BT, "DIAG_SUBSYS_BT"},
        {DIAG_SUBSYS_WCDMA, "DIAG_SUBSYS_WCDMA"},
        {DIAG_SUBSYS_HDR, "DIAG_SUBSYS_HDR"},
        {DIAG_SUBSYS_DIABLO, "DIAG_SUBSYS_DIABLO"},
        {DIAG_SUBSYS_TREX, "DIAG_SUBSYS_TREX"},
        {DIAG_SUBSYS_GSM, "DIAG_SUBSYS_GSM"},
        {DIAG_SUBSYS_UMTS, "DIAG_SUBSYS_UMTS"},
        {DIAG_SUBSYS_HWTC, "DIAG_SUBSYS_HWTC"},
        {DIAG_SUBSYS_FTM, "DIAG_SUBSYS_FTM"},
        {DIAG_SUBSYS_REX, "DIAG_SUBSYS_REX"},
        {DIAG_SUBSYS_OS, "DIAG_SUBSYS_OS"},
        {DIAG_SUBSYS_GPS, "DIAG_SUBSYS_GPS"},
        {DIAG_SUBSYS_WMS, "DIAG_SUBSYS_WMS"},
        {DIAG_SUBSYS_CM, "DIAG_SUBSYS_CM"},
        {DIAG_SUBSYS_HS, "DIAG_SUBSYS_HS"},
        {DIAG_SUBSYS_AUDIO_SETTINGS, "DIAG_SUBSYS_AUDIO_SETTINGS"},
        {DIAG_SUBSYS_DIAG_SERV, "DIAG_SUBSYS_DIAG_SERVICES"},
        {DIAG_SUBSYS_FS, "DIAG_SUBSYS_FS"},
        {DIAG_SUBSYS_PORT_MAP_SETTINGS, "DIAG_SUBSYS_PORT_MAP_SETTINGS"},
        {DIAG_SUBSYS_MEDIAPLAYER, "DIAG_SUBSYS_MEDIAPLAYER"},
        {DIAG_SUBSYS_QCAMERA, "DIAG_SUBSYS_QCAMERA"},
        {DIAG_SUBSYS_MOBIMON, "DIAG_SUBSYS_MOBIMON"},
        {DIAG_SUBSYS_GUNIMON, "DIAG_SUBSYS_GUNIMON"},
        {DIAG_SUBSYS_LSM, "DIAG_SUBSYS_LSM"},
        {DIAG_SUBSYS_QCAMCORDER, "DIAG_SUBSYS_QCAMCORDER"},
        {DIAG_SUBSYS_MUX1X, "DIAG_SUBSYS_MUX1X"},    //multiplexer
        {DIAG_SUBSYS_DATA1X, "DIAG_SUBSYS_DATA1X"},  //data
        {DIAG_SUBSYS_SRCH1X, "DIAG_SUBSYS_SRCH1X"},  //searcher
        {DIAG_SUBSYS_CALLP1X, "DIAG_SUBSYS_CALLP1X"},//call processing
        {DIAG_SUBSYS_APPS, "DIAG_SUBSYS_APPS"},
        {DIAG_SUBSYS_SETTINGS, "DIAG_SUBSYS_SETTINGS"},//settings
        {DIAG_SUBSYS_GSDI, "DIAG_SUBSYS_GSDI"},        // GENERIC SIM DRIVER interface
        {DIAG_SUBSYS_UIMDIAG, "DIAG_SUBSYS_UIMDIAG"},  // UIMDIAG (SIM card) subsystem == DIAG_SUBSYS_GSDI in newer versions
        {DIAG_SUBSYS_TMC, "DIAG_SUBSYS_TMC"},          // Task Main Control
        {DIAG_SUBSYS_USB, "DIAG_SUBSYS_USB"},          // USB (often contains USB state info)
        {DIAG_SUBSYS_PM, "DIAG_SUBSYS_PM"},            // Power management
        {DIAG_SUBSYS_DEBUG, "DIAG_SUBSYS_DEBUG"},
        {DIAG_SUBSYS_QTV, "DIAG_SUBSYS_QTV"},
        {DIAG_SUBSYS_CLKRGM, "DIAG_SUBSYS_CLKRGM"},//clock regime
        {DIAG_SUBSYS_DEVICES, "DIAG_SUBSYS_DEVICES"},
        {DIAG_SUBSYS_WLAN, "DIAG_SUBSYS_WLAN"},                                // 802.11 (Wi-Fi)
        {DIAG_SUBSYS_PS_DATA_LOGGING, "DIAG_SUBSYS_PS_DATA_LOGGING"},          // DATA PATH LOGGING
        {DIAG_SUBSYS_PS, "DIAG_SUBSYS_PS"},                                    //==DIAG_SUBSYS_PS_DATA_LOGGING
        {DIAG_SUBSYS_MFLO, "DIAG_SUBSYS_MFLO"},                                // MediaFLO
        {DIAG_SUBSYS_DTV, "DIAG_SUBSYS_DTV"},                                  // Digital TV
        {DIAG_SUBSYS_RRC, "DIAG_SUBSYS_RRC"},                                  // WCDMA RRC (Radio Resource Control) subsystem
        {DIAG_SUBSYS_PROF, "DIAG_SUBSYS_PROF"},                                // Miscellaneous profiling info
        {DIAG_SUBSYS_TCXOMGR, "DIAG_SUBSYS_TCXOMGR"},                          // TCXO (temperature compensated crystal oscillator) manager ???
        {DIAG_SUBSYS_NV, "DIAG_SUBSYS_NV"},                                    // non volatile memory (often contains nv item read/write info)
        {DIAG_SUBSYS_AUTOCONFIG, "DIAG_SUBSYS_AUTOCONFIG"},                    // Auto-configuration of diagnostic tools ???
        {DIAG_SUBSYS_PARAMS, "DIAG_SUBSYS_PARAMS"},                            // Parameter requied for debugging systems
        {DIAG_SUBSYS_MDDI, "DIAG_SUBSYS_MDDI"},                                // Mobile Display Digital Interface
        {DIAG_SUBSYS_DS_ATCOP, "DIAG_SUBSYS_DS_ATCOP"},                        // Data Services AT command processor ??
        {DIAG_SUBSYS_L4LINUX, "DIAG_SUBSYS_L4LINUX"},                          // Linux subsystem running at diag time
        {DIAG_SUBSYS_MVS, "DIAG_SUBSYS_MVS"},                                  // Multimode Voice Service
        {DIAG_SUBSYS_CNV, "DIAG_SUBSYS_CNV"},                                  // Compact NV subsystem
        {DIAG_SUBSYS_APIONE_PROGRAM, "DIAG_SUBSYS_APIONE_PROGRAM"},            // APIONE
        {DIAG_SUBSYS_HIT, "DIAG_SUBSYS_HIT"},                                  // Hardware Integration Test (HIT) subsystem
        {DIAG_SUBSYS_DRM, "DIAG_SUBSYS_DRM"},                                  // Digital Rights Management
        {DIAG_SUBSYS_DM, "DIAG_SUBSYS_DM"},                                    // Device management
        {DIAG_SUBSYS_FC, "DIAG_SUBSYS_FC"},                                    // Flow control
        {DIAG_SUBSYS_MEMORY, "DIAG_SUBSYS_MEMORY"},                            // Memory subsystem (often contains heap usage info)
        {DIAG_SUBSYS_FS_ALTERNATE, "DIAG_SUBSYS_FS_ALTERNATE"},                // Alternate file system (often contains file read/write info)
        {DIAG_SUBSYS_REGRESSION, "DIAG_SUBSYS_REGRESSION"},                    // Regression subsystem (often contains regression test info)
        {DIAG_SUBSYS_SENSORS, "DIAG_SUBSYS_SENSORS"},                          // Sensors (accelerometer, gyroscope, etc.)
        {DIAG_SUBSYS_FLUTE, "DIAG_SUBSYS_FLUTE"},                              // FLUTE (Firmware Load Update Over The Air)
        {DIAG_SUBSYS_ANALOG, "DIAG_SUBSYS_ANALOG"},                            // Analog die subsystem (often contains analog hardware info)
        {DIAG_SUBSYS_APIONE_PROGRAM_MODEM, "DIAG_SUBSYS_APIONE_PROGRAM_MODEM"},// APIONE programming for modem
        {DIAG_SUBSYS_LTE, "DIAG_SUBSYS_LTE"},                                  /* important: LTE subsystem id */
        {DIAG_SUBSYS_BREW, "DIAG_SUBSYS_BREW"},                                // BREW (Binary Runtime Environment for Wireless???)
        {DIAG_SUBSYS_PWRDB, "DIAG_SUBSYS_PWRDB"},                              // Power debug tool
        {DIAG_SUBSYS_CHORD, "DIAG_SUBSYS_CHORD"},                              // Chaos coordinator ??
        {DIAG_SUBSYS_SEC, "DIAG_SUBSYS_SEC"},                                  // Security subsystem (often contains crypto info)
        {DIAG_SUBSYS_TIME, "DIAG_SUBSYS_TIME"},                                // Time services (often contains time sync info)
        {DIAG_SUBSYS_Q6_CORE, "DIAG_SUBSYS_Q6_CORE"},                          //q6 core services, Hexagon DSP core (often contains DSP-related info)
        {DIAG_SUBSYS_COREBSP, "DIAG_SUBSYS_COREBSP"},                          // CoreBSP (core boot support package) ???
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
        {DIAG_SUBSYS_MFLO2, "DIAG_SUBSYS_MFLO2"},    // MediaFLO 2nd subsystem id (if needed)
                                                     /* Command code allocation:
                                                [0 - 1023]       - APPs
                                                [1024 - 65535]   - Reserved
                                            */
        {DIAG_SUBSYS_ULOG, "DIAG_SUBSYS_ULOG"},      // ULOG (micro-logging) subsystem
        {DIAG_SUBSYS_APR, "DIAG_SUBSYS_APR"},        // Asynchronous Packet Router (APR)
        {DIAG_SUBSYS_QNP, "DIAG_SUBSYS_QNP"},        // QNP (Qualcomm Network Processor??)
        {DIAG_SUBSYS_STRIDE, "DIAG_SUBSYS_STRIDE"},  // STRIDE (System Trace and Real-time Instrumentation for Diagnostics and Engineering) ???
        {DIAG_SUBSYS_OEMDPP, "DIAG_SUBSYS_OEMDPP"},  // OEM DPP (Device Performance Profiling??) //to read/write calibration to DPP partition
        {DIAG_SUBSYS_Q5_CORE, "DIAG_SUBSYS_Q5_CORE"},//q5 core services, Hexagon DSP core (often contains DSP-related info)
        {DIAG_SUBSYS_USCRIPT, "DIAG_SUBSYS_USCRIPT"},// USCRIPT (micro-scripting) subsystem
        {DIAG_SUBSYS_USCRIPT, "DIAG_SUBSYS_NAS"},
        /* NAS — often contains registration / serving cell info *///
        {DIAG_SUBSYS_CMAPI, "DIAG_SUBSYS_CMAPI"},                  // Call Manager API (CMAPI) subsystem
        {DIAG_SUBSYS_SSM, "DIAG_SUBSYS_SSM"},                      // System State Manager (SSM)???
        {DIAG_SUBSYS_MPOWER, "DIAG_SUBSYS_MPOWER"},                // MPower (power monitoring) subsystem
        {DIAG_SUBSYS_QDSS, "DIAG_SUBSYS_QDSS"},                    // Qualcomm Debug Subsystem (QDSS) QDSS STM???
        {DIAG_SUBSYS_CXM, "DIAG_SUBSYS_CXM"},                      // Coexistence Manager (CXM) subsystem ???
        {DIAG_SUBSYS_GNSS_SOC, "DIAG_SUBSYS_GNSS_SOC"},            // Secondary GNSS system
        {DIAG_SUBSYS_TTLITE, "DIAG_SUBSYS_TTLITE"},                // TT Lite (a lightweight task tracing tool??)
        {DIAG_SUBSYS_FTM_ANT, "DIAG_SUBSYS_FTM_ANT"},              // FTM ANT (antenna testing in factory test mode??)
        {DIAG_SUBSYS_MLOG, "DIAG_SUBSYS_MLOG"},                    // MLog (micro-logging) subsystem
        {DIAG_SUBSYS_LIMITSMGR, "DIAG_SUBSYS_LIMITSMGR"},          // Limits Manager (often contains info about various limits in the system)
        {DIAG_SUBSYS_EFSMONITOR, "DIAG_SUBSYS_EFSMONITOR"},
        {DIAG_SUBSYS_DISPLAY_CALIBRATION, "DIAG_SUBSYS_DISPLAY_CALIBRATION"},// Display calibration subsystem (often contains display calibration info)
        {DIAG_SUBSYS_VERSION_REPORT, "DIAG_SUBSYS_VERSION_REPORT"},
        {DIAG_SUBSYS_DS_IPA, "DIAG_SUBSYS_DS_IPA"},
        {DIAG_SUBSYS_SYSTEM_OPERATIONS, "DIAG_SUBSYS_SYSTEM_OPERATIONS"},
        {DIAG_SUBSYS_CNSS_POWER, "DIAG_SUBSYS_CNSS_POWER"},
        {DIAG_SUBSYS_LWIP, "DIAG_SUBSYS_LWIP"},
        {DIAG_SUBSYS_IMS_QVP_RTP, "DIAG_SUBSYS_IMS_QVP_RTP"},
        {DIAG_SUBSYS_STORAGE, "DIAG_SUBSYS_STORAGE"},
        {DIAG_SUBSYS_WCI2, "DIAG_SUBSYS_WCI2"},
        {DIAG_SUBSYS_AOSTLM_TEST, "DIAG_SUBSYS_AOSTLM_TEST"},
        {DIAG_SUBSYS_RESERVED_OEM_0, "DIAG_SUBSYS_RESERVED_OEM_0"},
        {DIAG_SUBSYS_RESERVED_OEM_1, "DIAG_SUBSYS_RESERVED_OEM_1"},
        {DIAG_SUBSYS_RESERVED_OEM_2, "DIAG_SUBSYS_RESERVED_OEM_2"},
        {DIAG_SUBSYS_RESERVED_OEM_3, "DIAG_SUBSYS_RESERVED_OEM_3"},
        {DIAG_SUBSYS_RESERVED_OEM_4, "DIAG_SUBSYS_RESERVED_OEM_4"},
        {DIAG_SUBSYS_LEGACY, "DIAG_SUBSYS_LEGACY"},
        {-1, NULL}};


#endif /* DIAG_DEFS_H */