#pragma once

#include "diag_common.h"

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <functional>

// Нативные коды логов Qualcomm
#define LOG_LTE_RRC_OTA_C ((uint16_t) (0xB0C0))
#define LOG_LTE_RRC_MIB_C ((uint16_t) (0xB0C1))
#define LOG_LTE_RRC_SERV_CELL_INFO_C ((uint16_t) (0xB0C2))

// Числовые типы для полной совместимости с донором
struct LteCellIdentity {
    uint32_t cell_id = 0;       // 28-bit ECI
    uint16_t tac = 0;
    uint32_t mcc = 0;
    uint32_t mnc = 0;
    uint8_t mnc_digit_count = 0;
    uint32_t earfcn = 0;
    uint32_t ul_earfcn = 0;
    uint16_t pci = 0;
    uint32_t band = 0;
    uint8_t dl_bw = 0;
    uint8_t ul_bw = 0;
    uint8_t allowed_access = 0;

    uint16_t sfn = 0;
    uint8_t phich_duration = 0;
    uint8_t phich_resource = 0;

    bool valid = false;
};

struct LteMeasReport {
    uint32_t earfcn = 0;
    int pci = -1;
    int rsrp = 0;
    int rsrq = 0;
    bool is_serving = false;
};

class DiagLteRrcParser {
public:
    // ИСПРАВЛЕНО: Заменили битовые сдвиги на HEX, чтобы парсер ничего не сожрал!
    enum FilterFlags : uint32_t {
        PARSE_CELL_IDENTITY = 0x0001,  // Парсить паспорта БС (MCC/MNC/TAC/CID)
        PARSE_SIB_NEIGHBORS = 0x0002,  // Парсить частотные карты соседей из SIB4/5
        PARSE_MEASUREMENTS  = 0x0004,  // Парсить децибелы сигнала (RSRP/RSRQ)
        PARSE_ALL           = 0xFFFFFFFF
    };

    // Честный колбэк
    using IdentityCallback = std::function<LteCellIdentity>;

    DiagLteRrcParser() = default;
    ~DiagLteRrcParser() = default;

    void set_identity_callback(IdentityCallback cb) { id_cb_ = std::move(cb); }
    void set_debug(bool on) { debug_ = on; }
    void set_filter_mask(uint32_t flags) { filter_mask_ = flags; }

    static std::vector<uint16_t> handled_log_codes();
    bool parse(const uint8_t *buf, size_t len);

    const LteCellIdentity &identity() const { return id_; }

    std::vector<LteCell> &sib1_cells() { return sib1_cells_; }
    const std::vector<LteCell> &sib1_cells() const { return sib1_cells_; }

    std::vector<LteCell> &serv_cells() { return serv_cells_; }
    const std::vector<LteCell> &serv_cells() const { return serv_cells_; }

    std::vector<LteMeasReport> &meas_reports() { return meas_reports_; }
    const std::vector<LteMeasReport> &meas_reports() const { return meas_reports_; }

private:
    bool parse_rrc_ota(const uint8_t *p, size_t plen);
    bool parse_rrc_mib(const uint8_t *p, size_t plen);
    bool parse_rrc_sib1(const uint8_t *p, size_t plen,
                        uint32_t earfcn, uint16_t pci);
    bool parse_rrc_ul_dcch(const uint8_t *p, size_t plen,
                           uint32_t earfcn, uint16_t pci);
    bool parse_rrc_serving_cell_info(const uint8_t *p, size_t plen);

    void stash_serving_cell();

    LteCellIdentity id_;
    IdentityCallback id_cb_;
    std::vector<LteCell> sib1_cells_;
    std::vector<LteCell> serv_cells_;
    std::vector<LteMeasReport> meas_reports_;
    std::unordered_map<int, uint32_t> neigh_freq_map_;
    uint32_t filter_mask_ = FilterFlags::PARSE_ALL;
    bool debug_ = true;
};
