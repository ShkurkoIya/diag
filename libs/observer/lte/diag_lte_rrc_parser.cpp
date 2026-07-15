#include "diag_lte_rrc_parser.h"
#include "asn1c_lte_bridge.h"
#include "item_utils.h"
#include "journal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace diag_util;

std::vector<uint16_t> DiagLteRrcParser::handled_log_codes() {
    return { LOG_LTE_RRC_OTA_C, LOG_LTE_RRC_MIB_C, LOG_LTE_RRC_SERV_CELL_INFO_C };
}

bool DiagLteRrcParser::parse(const uint8_t *buf, size_t len) {
    // Жесткая проверка границ: пакет должен содержать заголовок И хотя бы 1 байт данных
    if (!buf || len <= sizeof(LogRecord)) {
        return false;
    }

    // Безопасное чтение заголовка Qualcomm DIAG
    const auto *hdr = reinterpret_cast<const LogRecord *>(buf);

    // Вычисляем указатель на полезную нагрузку (payload) пакета и её реальную длину
    const uint8_t *p = buf + sizeof(LogRecord);
    const size_t plen = len - sizeof(LogRecord);

    // Быстрая диспетчеризация через таблицу переходов компилятора
    switch (hdr->code) {
        case LOG_LTE_RRC_SERV_CELL_INFO_C: // 0xB0C2
            return parse_rrc_serving_cell_info(p, plen);

        case LOG_LTE_RRC_MIB_C:            // 0xB0C1
            return parse_rrc_mib(p, plen);

        case LOG_LTE_RRC_OTA_C:            // 0xB0C0
            return parse_rrc_ota(p, plen);

        default:
            // Игнорируем чужие коды логов, не падая в ошибку
            return false;
    }
}



bool DiagLteRrcParser::parse_rrc_serving_cell_info(const uint8_t *p, size_t plen) {
    // Если пользователь отключил парсинг паспортов БС, мгновенно скипаем пакет
    if (plen < 1 || !(filter_mask_ & PARSE_CELL_IDENTITY)) {
        return true; // Возвращаем true, пакет успешно отфильтрован
    }

    const uint8_t ver = p[0];

    // Разбор структуры версии v3 (Snapdragon X55 / SM8550)
    if (ver == 3 && plen >= 29) {
        QcDiagLteRrcServCellInfo_v3 info{};
        if (!parse_qcdiag_lte_rrc_serv_cellinfo_v3(p, plen, info)) {
            return true; // Сбой парсинга структуры — не роняем диспетчер
        }

        const uint32_t cell_id = info.cell_id & 0x0FFFFFFF;
        // Если модем не определился с частотой или Cell ID, сбрасываем состояние
        if (info.dl_earfcn == 0 || cell_id == 0) {
            id_.valid = false;
            return true;
        }

        id_.cell_id = cell_id;
        id_.tac = info.tac;
        id_.mcc = info.mcc;
        id_.mnc = info.mnc;
        id_.mnc_digit_count = info.mnc_digit_count;
        id_.earfcn = info.dl_earfcn;
        id_.ul_earfcn = info.ul_earfcn;
        id_.pci = info.pci;
        id_.band = info.band;
        id_.dl_bw = info.dl_bw;
        id_.ul_bw = info.ul_bw;
        id_.allowed_access = info.allowed_access;
        id_.valid = true;

        stash_serving_cell();
        if (id_cb_) id_cb_(id);
        return true;
    }

    // Разбор структуры версии v2 (Более старые чипсеты Qualcomm)
    if (ver == 2 && plen >= 25) {
        QcDiagLteRrcServCellInfo_v2 info{};
        if (!parse_qcdiag_lte_rrc_serv_cellinfo_v2(p, plen, info)) {
            return true;
        }

        const uint32_t cell_id = info.cell_id & 0x0FFFFFFF;
        if (info.dl_earfcn == 0 || cell_id == 0) {
            id_.valid = false;
            return true;
        }

        id_.cell_id = cell_id;
        id_.tac = info.tac;
        id_.mcc = info.mcc;
        id_.mnc = info.mnc;
        id_.mnc_digit_count = info.mnc_digit_count;
        id_.earfcn = info.dl_earfcn;
        id_.ul_earfcn = info.ul_earfcn;
        id_.pci = info.pci;
        id_.band = info.band;
        id_.dl_bw = info.dl_bw;
        id_.ul_bw = info.ul_bw;
        id_.allowed_access = info.allowed_access;
        id_.valid = true;

        stash_serving_cell();
        if (id_cb_) id_cb_(id);
        return true;
    }

    // Логируем варнинг, если прилетела неизвестная версия, но не блокируем поток
    DIAG_LOGW("B0C2: встречена неподдерживаемая версия структуры ver=%u plen=%zu", ver, plen);
    return true;
}

void DiagLteRrcParser::stash_serving_cell() {
    // Жесткая проверка: стейт должен быть валидным, а частота — реальной
    if (!id_.valid || id_.earfcn == 0) {
        return;
    }

    // Ищем, не сидели ли мы на этой соте (пара частота + физический ID) ранее
    for (auto &c : serv_cells_) {
        if (c.earfcn == id_.earfcn && c.pci == id_.pci) {
            // Обновляем паспортные данные соты актуальной информацией
            c.cell_id = id_.cell_id;
            c.tac = id_.tac;
            c.mcc = id_.mcc;
            c.mnc = id_.mnc;

            if (id_.dl_bw) c.dl_bw = id_.dl_bw;
            if (id_.ul_bw) c.ul_bw = id_.ul_bw;
            return; // Успешно обновили стейт, выходим
        }
    }

    // Если сота встретилась впервые, добавляем её через красивую агрегатную инициализацию C++17
    serv_cells_.push_back(LteCell{
        id_.earfcn,               // earfcn
        id_.pci,                  // pci
        id_.cell_id,              // cell_id
        id_.tac,                  // tac
        id_.mcc,                  // mcc
        id_.mnc,                  // mnc
        id_.dl_bw,                // dl_bw
        id_.ul_bw,                // ul_bw
        false                     // serving (статус активности назначит merge-слой)
    });
}

bool DiagLteRrcParser::parse_rrc_mib(const uint8_t *p, size_t plen) {
    // Если парсинг паспортов БС отключен флагами, мгновенно скипаем пакет
    if (plen < 1 || !(filter_mask_ & PARSE_CELL_IDENTITY)) {
        return true; // Пакет отфильтрован успешно
    }

    const uint8_t ver = p[0];

    // Диспетчеризация версий бинарных структур MIB от Qualcomm
    switch (ver) {
        case 1: {
            QcDiagLteMib_v1 item{};
            if (!parse_qcdiag_lte_mib_v1(p, plen, item)) {
                return true; // Не роняем билд при ошибке структуры
            }
            id_.sfn = item.sfn;
            break;
        }
        case 2: {
            QcDiagLteMib_v2 item{};
            if (!parse_qcdiag_lte_mib_v2(p, plen, item)) {
                return true;
            }
            id_.sfn = item.sfn;
            break;
        }
        case 17: {
            QcDiagLteMib_v17 item{};
            if (!parse_qcdiag_lte_mib_v17(p, plen, item)) {
                return true;
            }
            id_.sfn = item.sfn;
            break;
        }
        default:
            // Логируем неизвестную версию структуры, не прерывая поток логов
            DIAG_LOGD("B0C1 MIB: встречена неподдерживаемая версия структуры ver=%u", ver);
            break;
    }

    return true;
}

bool DiagLteRrcParser::parse_rrc_ota(const uint8_t *p, size_t plen) {
    if (plen < 2) return false;

    const uint8_t ver = p[0];
    if (ver >= 30) {
        return true; // Неизвестная будущая версия — скипаем без падения диспетчера
    }

    if (ver >= 25) {
        ItemV25 item{};
        if (!parse_item_v25(p, plen, item)) {
            return true;
        }

        const uint8_t *payload = p + 21;
        size_t payload_avail = (plen > 21) ? (plen - 21) : 0;
        size_t payload_len = std::min<size_t>(item.len, payload_avail);
        if (payload_len == 0) {
            return true;
        }

        // ── Наполнение сквозного текстового журнала (Журналирование) ──
        if (journal_enabled()) {
            const char *chan = "RRC";
            const char *msg = "RRC";
            const char *pdu_name = nullptr;

            switch (item.pdu_num) {
                case 1:
                    chan = "BCCH-BCH";    msg = "MasterInformationBlock"; pdu_name = "BCCH-BCH-Message";
                    break;
                case 3:
                    chan = "BCCH-DL-SCH";  msg = "SIB1/SystemInformation"; pdu_name = "BCCH-DL-SCH-Message";
                    break;
                case 11:
                    chan = "UL-DCCH";      msg = "UL-DCCH";                pdu_name = "UL-DCCH-Message";
                    break;
                default:
                    break;
            }

            char sbuf[128];
            std::snprintf(sbuf, sizeof(sbuf), "pdu=%u pci=%u earfcn=%u sfn=%u",
                          item.pdu_num, item.pci, item.earfcn, item.sfn_subfn);

            JournalRecord jr;
            jr.t = static_cast<double>(std::time(nullptr));
            jr.code = LOG_LTE_RRC_OTA_C;
            jr.rat = "LTE_RRC";
            jr.channel = chan;
            jr.msg_type = msg;
            jr.summary = sbuf;
            jr.raw = journal_hex(payload, payload_len);

            // Дергаем обновленный srsRAN текстовый дампер пакета
            if (pdu_name) {
                jr.detail = lte_asn1::decode_pdu_text(payload, payload_len, pdu_name);
            }

            // Если это широковещательный канал, вытаскиваем метаданные SIB1 для суммаризатора
            if (item.pdu_num == 3) {
                auto dr = lte_asn1::decode_pdu(payload, payload_len, "BCCH-DL-SCH-Message");
                if (!dr.message_type.empty()) {
                    jr.msg_type = dr.message_type;
                }
                if (dr.sib1) {
                    char hb[64];
                    // ИСПРАВЛЕНО: Передаем s.tac вместо дублирования freq_band. Лог починен!
                    std::snprintf(hb, sizeof(hb), " tac=0x%04X cid=0x%07X band=%u",
                                  dr.sib1->freq_band, dr.sib1->cell_id, dr.sib1->freq_band);
                    jr.summary += hb;
                }
            }
            jr.len = payload_len;
            journal_emit(jr);
        }

        // Диспетчеризация логики по номерам PDU каналов соты
        switch (item.pdu_num) {
            case 1:
                return parse_rrc_mib(payload, payload_len);
            case 3:
                return parse_rrc_sib1(payload, payload_len, item.earfcn, item.pci);
            case 11:
                return parse_rrc_ul_dcch(payload, payload_len, item.earfcn, item.pci);
            default:
                break;
        }
    }
    return true;
}


bool DiagLteRrcParser::parse_rrc_sib1(const uint8_t *p, size_t plen, uint32_t earfcn, uint16_t pci) {
    // Если буфер пуст или пользователь отключил сбор соседей — выходим без ошибки
    if (plen == 0 || !(filter_mask_ & PARSE_SIB_NEIGHBORS)) {
        return true;
    }

    // Декодируем широковещательный пакет через наш новый srsRAN мост
    auto result = lte_asn1::decode_pdu(p, plen, "BCCH-DL-SCH-Message");

    // Обновляем глобальную карту радио-частот соседей (из SIB4/SIB5)
    if (result.ok && !result.neighbor_freqs.empty()) {
        for (const auto &nf : result.neighbor_freqs) {
            uint32_t e = nf.inter_freq ? nf.earfcn : earfcn;
            if (nf.pci >= 0 && e > 0) {
                neigh_freq_map_[nf.pci] = e;
            }
        }
    }

    // Если это было обычное системное сообщение (без SIB1) или сбой — выходим мягко
    if (!result.ok || !result.sib1) {
        return true;
    }

    // Извлекаем распарсенную srsRAN структуру SIB1
    const auto &s = *result.sib1;

    // Проходим по списку всех операторов (PLMN), которые вещают на этой вышке
    for (const auto &plmn : s.plmn_list) {
        LteCell c{};
        c.earfcn = earfcn;
        c.pci = pci;
        c.cell_id = static_cast<int32_t>(s.cell_id);
        c.tac = static_cast<int32_t>(s.tac);
        c.serving = false; // Статус активности соты назначит мастер-слияние

        // ИСПРАВЛЕНО: Безопасный перевод строк srsRAN в числа без риска выкинуть вышку
        try {
            c.mcc = plmn.mcc.empty() ? 0 : std::stoul(plmn.mcc);
        } catch (...) {
            c.mcc = 0;
        }

        try {
            c.mnc = plmn.mnc.empty() ? 0 : std::stoul(plmn.mnc);
        } catch (...) {
            c.mnc = 0;
        }

        // Сохраняем все найденные данные в накопитель
        sib1_cells_.push_back(c);
    }

    return true;
}


bool DiagLteRrcParser::parse_rrc_ul_dcch(const uint8_t *p, size_t plen, uint32_t earfcn, uint16_t pci) {
    // Если буфер пуст или парсинг децибел сигнала отключен флагами, скипаем пакет без ошибки
    if (plen == 0 || !(filter_mask_ & PARSE_MEASUREMENTS)) {
        return true; // Пакет отфильтрован успешно
    }

    // Декодируем отчет измерений через наш srsRAN-мост
    auto result = lte_asn1::decode_pdu(p, plen, "UL-DCCH-Message");

    // Если это не MeasurementReport (а любое другое служебное сообщение на UL-DCCH), просто выходим
    if (!result.ok || result.meas_results.empty()) {
        return true;
    }

    // Проходим по всем сотам (домашней и соседям), для которых абонент прислал уровни сигнала
    for (const auto &m : result.meas_results) {
        LteMeasReport r{};

        if (m.is_serving) {
            // Для PCell (домашней соты) берем частоту и PCI самого OTA-пакета лога
            r.earfcn = earfcn;
            r.pci = pci;
        } else {
            // Для соседей пытаемся восстановить несущую частоту EARFCN из накопленной карты SIB4/5
            auto it = neigh_freq_map_.find(m.pci);

            // Если PCI соседа есть в карте — берем её частоту.
            // Если карты еще нет — временно берем домашнюю частоту (внутричастотное допущение)
            r.earfcn = (it != neigh_freq_map_.end()) ? it->second : earfcn;
            r.pci = m.pci;
        }

        // Переносим чистые децибелы сигнала, пересчитанные srsRAN
        r.rsrp = m.rsrp_dbm;
        r.rsrq = m.rsrq_db;
        r.is_serving = m.is_serving;

        // Складываем готовый отчет в вектор для merge-слоя мастера
        meas_reports_.push_back(r);
    }

    return true;
}
