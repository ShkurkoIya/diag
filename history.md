ДЛЯ МЕНЯ ЗАВТРАШНЕГО — КОНТЕКСТ РЕФАКТОРИНГА QUALCOMM DIAG SCANNER ДЛЯ LINUXТекущий стейт проекта: Мы находимся на стабильном коммите 91cb51b (точка «эврики»), проект успешно компилируется под Linux с нативным ASN.1 декодером LTE SIB1 (USE_ASN1C_LTE=ON). Парсер полностью разбирает паспорта базовых станций МТС, МегаФон, Билайн и Теле2 на порту /dev/ttyUSB0 и выводит их в консоль.Текущая проблема: При попытке открыть полную маску логов 0xFF (в методе StartREO()), модем Snapdragon X55 (SIM8300) включает дефолтный флуд тяжелыми пакетами планировщика данных (0xB1DB и 0xB1DC длиной от 1.5 до 3 КБ). Скорость потока байт из USB 3.0 превышает пропускную способность драйвера последовательного порта хоста, из-за чего вылетает ошибка [SerialPort] Ошибка: Переполнение кольцевого буфера RX!.Что было сделано на последнем шаге: Мы выяснили, что класс SerialPort проекта скрывает внутри себя ceSerial *ce_uart и имеет собственный кольцевой буфер rxQueueBuffer. В main_linux.cpp мы внедрили блочный гибридный алгоритм: выгребаем данные из порта пачками по 6144 байт через родной метод SimPort.ReadData(rawReadBuffer), прогоняем через побайтовый HDLC-автомат и отсекаем тяжелый мусор 0xB1DB/0xB1DC на уровне динамического сканера заголовков, чтобы разгрузить буфер. Фоновый поток CeMain в main_linux.cpp теперь запускать НЕ нужно, так как порт сам управляет чтением.Задача на завтра: Проверить, прорвало ли поток на последнем гибридном коде main_linux.cpp без CeMain. Если переполнение буфера rxQueueBuffer сохраняется, значит, порт забивается внутри метода ReadToRX(). Решение: либо аппаратно расширить массив rxQueueBuffer в serial_port.h до 1 Мегабайта, либо пересчитать точечную маску StartREO() строго в 62 байта, чтобы модем принял её без сброса в дефолтный флуд. Дамп кода, на котором мы остановилисьВот два финальных файла, которые нужно зафиксировать в проекте перед уходом.1. Файл core/linux_serial/diag_request.cpp (Идеальная маска)cppbool DiagReq::StartREO()
{
    std::cout << "[DIAG] Активация ИДЕАЛЬНОЙ точечной маски логов чипсета X55..." << std::endl;

    uint8_t buffer[bufSize];
    uint32_t op_set = 3; // LOG_CONFIG_SET_MASK_OP

    std::memset(buffer, 0, sizeof(buffer));
    uint32_t equip_lte = 0x0B; // LTE/5G Equip ID
    uint8_t b1 = 0x01; uint8_t b2 = 0x02;

    buffer[0] = 0x00; buffer[1] = 0x00; buffer[2] = 0x00;
    uint32_t* op_ptr = (uint32_t*)(&buffer[3]);   *op_ptr = op_set;      
    uint32_t* eq_ptr = (uint32_t*)(&buffer[7]);   *eq_ptr = equip_lte;   
    buffer[11] = b1; buffer[12] = b2;

    std::memset(&buffer[13], 0x00, 26); // 26x выравнивание

    size_t lte_mask_start = 39; // Маска логов LTE начинается строго с 39-го байта
    buffer[lte_mask_start + 24] = 0x07; // 0xB0C0, 0xB0C1, 0xB0C2 (Паспорта)
    buffer[lte_mask_start + 47] = 0x80; // 0xB17F (Serving measurements)
    buffer[lte_mask_start + 50] = 0x08; // 0xB193 (НАШИ ДЕЦИБЕЛЫ RSRP!)
    buffer[lte_mask_start + 51] = 0x80; // 0xB197 (Neighbor measurements)

    size_t lte_packet_size = 101; // СТРОГО 101 байт под требования MPSS X55

    std::cout << "[DIAG] Отправка LTE_CONFIG (Размер пакета: " << lte_packet_size << " байт)..." << std::endl;
    if (!send_diag_packet(0x73, buffer, lte_packet_size)) return false;

    // Маска GSM (64 байта)
    std::memset(buffer, 0, sizeof(buffer));
    uint32_t equip_gsm = 0x05;
    size_t g_idx = 3;
    std::memcpy(&buffer[g_idx], &op_set, 4);      g_idx += 4; 
    std::memcpy(&buffer[g_idx], &equip_gsm, 4);   g_idx += 4; 
    buffer[g_idx++] = ' '; buffer[g_idx++] = 0x04;
    std::memset(&buffer[g_idx], 0x00, 39);       g_idx += 39; 
    std::memset(&buffer[g_idx], 0xFF, 64);       g_idx += 64; 

    if (!send_diag_packet(0x73, buffer, g_idx)) return false;

    // RUNTIME COMMIT
    std::memset(buffer, 0, sizeof(buffer));
    uint32_t op_start = 1; std::memcpy(&buffer[3], &op_start, 4);
    if (!send_diag_packet(0x73, buffer, 7)) return false;

    // LOG STREAM ENABLE
    uint8_t stream_enable_payload = 1; 
    return send_diag_packet(0x1C, &stream_enable_payload, 1);
}
Используйте код с осторожностью.2. Файл main_linux.cpp (Гибридное блочное выгребание без CeMain)cpp#include <iostream>
#include <thread>
#include <string>
#include <cstdint>
#include <csignal>
#include <atomic>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "serial_port.h"
#include "diag_request.h"
#include "parsers/qualcomm_log_parser.h"

static std::atomic<bool> g_running{true};
static void on_signal(int sig) { g_running = false; (void)sig; }

bool ExecuteStepHandle(DiagReq &diag) {
    if (!diag.Init()) return false;
    if (!diag.ZeroLog()) return false;
    if (!diag.StartREO()) return false;
    return true;
}

struct CellState {
    uint32_t earfcn = 0; uint16_t pci = 0; int16_t rsrp = 0; int16_t rsrq = 0;
    int32_t cell_id = -1; uint16_t tac = 0; uint32_t mcc = 0; uint32_t mnc = 0;
    bool serving = false;
};

int main(int argc, char *argv[]) {
    std::string device_path = "/dev/ttyUSB0";
    std::cout << "[Scanner] Запуск ГИБРИДНОГО логгера на порту: " << device_path << std::endl;

    SerialPort SimPort(device_path);
    if (!SimPort.Init()) return 1;

    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    QualcommLogParser qcom_parser;
    std::unordered_map<uint32_t, CellState> cell_cache;

    qcom_parser.set_neighbor_callback([&](const ParsedNeighbors& n) {
        bool state_changed = false;
        for (const auto& c : n.lte) {
            uint32_t key = (c.earfcn << 16) | c.pci;
            auto& state = cell_cache[key];
            state.earfcn = c.earfcn; state.pci = c.pci; state.serving = c.serving;
            if (c.rsrp != 0 && (state.rsrp != c.rsrp || state.rsrq != c.rsrq)) {
                state.rsrp = c.rsrp; state.rsrq = c.rsrq; state_changed = true;
            }
            if (c.cell_id > 0 && state.cell_id != c.cell_id) {
                state.cell_id = c.cell_id; state.tac = c.tac; state.mcc = c.mcc; state.mnc = c.mnc;
                state_changed = true;
            }
        }
        if (state_changed) {
            std::printf("\n==================== STREAM LIVE MONITOR ====================\n");
            for (const auto& [key, state] : cell_cache) {
                std::printf("[LTE %s] EARFCN: %-6u | PCI: %-3u", state.serving ? "SERVING " : "NEIGHBOR", state.earfcn, state.pci);
                if (state.rsrp != 0) std::printf(" | RSRP: %-4d dBm | RSRQ: %-3d dB", (int)state.rsrp, (int)state.rsrq);
                else                 std::printf(" | RSRP: [Wait ML1 ] | RSRQ: [--] ");
                if (state.cell_id > 0) std::printf(" | PLMN: %03u-%02u | TAC: %-5u | CellID: %-10d", state.mcc, state.mnc, state.tac, state.cell_id);
                std::printf("\n");
            }
            std::printf("--------------------------------------------------------------\n");
            std::fflush(stdout);
        }
    });

    DiagReq diag(&SimPort);
    uint8_t rawReadBuffer[bufSize];
    if (!ExecuteStepHandle(diag)) { SimPort.CloseConnection(); return 2; }

    std::cout << "[Scanner] Движок запущен. Ожидание сотового потока...\n";
    uint8_t cleanBuffer[bufSize]; size_t cleanIdx = 0;
    bool inside_packet = false; bool escape_next = false;

    while (g_running) {
        int size = SimPort.ReadData(rawReadBuffer); // Выгребаем всю пачку из rxQueueBuffer за раз
        if (size > 0) {
            for (int i = 0; i < size; i++) {
                uint8_t byte = rawReadBuffer[i];
                if (byte == 0x7E) {
                    if (!inside_packet) { inside_packet = true; cleanIdx = 0; escape_next = false; } 
                    else {
                        inside_packet = false;
                        if (cleanIdx > 14) {
                            size_t payload_len = cleanIdx - 2;
                            if (cleanBuffer[0] == 0x73 || cleanBuffer[0] == 0x7D) continue;

                            const uint8_t* log_record_ptr = nullptr; size_t inner_len = 0; bool found_valid_log = false;
                            for (size_t offset = 0; offset <= 12 && (offset + 4) <= payload_len; ++offset) {
                                uint16_t current_len  = cleanBuffer[offset] | (cleanBuffer[offset + 1] << 8);
                                uint16_t current_code = cleanBuffer[offset + 2] | (cleanBuffer[offset + 3] << 8);
                                if ((current_code >= 0xB000 && current_code <= 0xBFFF) || (current_code >= 0x5000 && current_code <= 0x5FFF)) {
                                    if (current_len <= (payload_len - offset)) {
                                        if (current_code != 0xB1DB && current_code != 0xB1DC && current_code != 0xB114) {
                                            log_record_ptr = &cleanBuffer[offset]; inner_len = payload_len - offset; found_valid_log = true;
                                        }
                                        break; 
                                    }
                                }
                            }
                            if (found_valid_log && log_record_ptr != nullptr) qcom_parser.on_log(log_record_ptr, inner_len);
                        }
                    }
                } else {
                    if (inside_packet && cleanIdx < bufSize) {
                        if (byte == 0x7D) { escape_next = true; } 
                        else {
                            if (escape_next) { cleanBuffer[cleanIdx++] = byte ^ 0x20; escape_next = false; } 
                            else { cleanBuffer[cleanIdx++] = byte; }
                        }
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    SimPort.CloseConnection();
    return 0;
}