#include <iostream>
#include <thread>
#include <string>
#include <cstdint>
#include <csignal>
#include <atomic>
#include <chrono>
#include <cstring>

// Наша асинхронная реактивная инфраструктура TTY
#include "serial_port.h"
#include "serial_port_stream.h"
#include "hdlc_serializer.h"
#include "hdlc_stream_parser.h"
#include "diag_request.h"
#include "ce_thread.h"

// Главный диспетчер парсера Qualcomm
#include "parsers/qualcomm_log_parser.h"

static std::atomic<bool> g_running{true};
static void on_signal(int sig)
{
    g_running = false;
    (void)sig;
}

// Оркестратор отправки стартовых масок в модем SIMCom
bool ExecuteStepHandle(DiagRequest &diag)
{
    if (!diag.Init())
        return false;
    if (!diag.StartREO())
        return false;
    return true;
}

static void print_help(const char *argv0)
{
    std::fprintf(stderr, "Linux Usage: %s [/dev/ttyUSB0]\n", argv0);
}

int main(int argc, char *argv[])
{
    // ИСПРАВЛЕНО: берем первый переданный аргумент argv[1], а не весь массив argv
    std::string device_path = (argc > 1) ? argv[1] : "/dev/ttyUSB0";

    if (device_path == "-h" || device_path == "--help")
    {
        // ИСПРАВЛЕНО: передаем имя программы argv[0] для хелпа
        print_help(argv[0]);
        return 0;
    }

    std::cout << "[Scanner] Запуск Edge-зонда на порту: " << device_path << std::endl;

    // ... дальше весь твой рабочий код мейна идет без изменений ...

    SerialPort SimPort(device_path);
    if (!SimPort.Init())
    {
        std::cerr << "[Scanner] КРИТИЧЕСКАЯ ОШИБКА: Не удалось открыть порт " << device_path << std::endl;
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    DiagRequest diag(&SimPort);
    bool closeConnection = false;

    // Инициализируем маски и тушим флуд на аппаратном уровне модема
    if (!ExecuteStepHandle(diag))
    {
        SimPort.CloseConnection();
        std::cerr << "[Scanner] Ошибка инициализации масок DIAG логов.\n";
        return 2;
    }

    // Запускаем фоновый поток CeThread для непрерывного забора байт из USB в очередь SimPort
    std::thread t0(CeMain, std::ref(SimPort), std::ref(closeConnection));

    // Инициализируем оригинальный парсер Qualcomm верхнего уровня
    QualcommLogParser qcom_parser;

    // ВЕШАЕМ НАШ ОРИГИНАЛЬНЫЙ МУЛЬТИ-RAT КОЛЛБЕК ВЫВОДА (Сохранен на 100%)
    qcom_parser.set_neighbor_callback([&](const ParsedNeighbors &n)
                                      {
        std::printf("\n==================== STREAM LIVE MONITOR (MULTI-RAT) ====================\n");
        
        if (!n.lte.empty()) {
            std::printf("[4G LTE Layer] ──────────────────────────────────────────────────────────\n");
            for (const auto& c : n.lte) {
                std::printf("  [%s] EARFCN: %-5u | PCI: %-3u", c.serving ? "SERVING" : "NEIGHBR", c.earfcn, c.pci);
                if (c.rsrp != 0) {
                    std::printf(" | RSRP: %-4d dBm | RSRQ: %-3d dB | RSSI: %-4d dBm", (int)c.rsrp, (int)c.rsrq, (int)c.rssi);
                } else {
                    std::printf(" | RSRP: [Scanning] | RSRQ: [--] | RSSI: [----]");
                }
                if (c.cell_id > 0) {
                    std::printf(" | PLMN: %03u-%02u | TAC: %-5u | CellID: %-10d | BW: %uMHz", 
                                c.mcc, c.mnc, c.tac, c.cell_id, c.dl_bw);
                }
                std::printf("\n");
            }
        }

        if (!n.nr.empty()) {
            std::printf("[5G NR Layer] ───────────────────────────────────────────────────────────\n");
            for (const auto& c : n.nr) {
                std::printf("  [%s] NR-ARFCN: %-6u | PCI: %-3u", c.serving ? "SERVING" : "NEIGHBR", c.nrarfcn, c.pci);
                if (c.ss_rsrp != 0) {
                    std::printf(" | ssb-RSRP: %-4d dBm | ssb-RSRQ: %-3d dB", (int)c.ss_rsrp, (int)c.ss_rsrq);
                } else {
                    std::printf(" | ssb-RSRP: [Wait ML1] | ssb-RSRQ: [--]");
                }
                if (c.cell_id > 0) {
                    std::printf(" | PLMN: %03u-%02u | TAC: %-5u | CellID: %d", 
                                c.mcc, c.mnc, (uint32_t)c.tac, (int)c.cell_id);
                }
                std::printf("\n");
            }
        }
        std::printf("-------------------------------------------------------------------------\n");
        std::fflush(stdout); });

    // 1. Создаем наш реактивный HDLC конвейер (Использует HdlcConfig по умолчанию)
    HdlcStreamParser hdlc_parser;

    // 2. Декларативно настраиваем реакцию на появление ГОТОВОГО распакованного фрейма
    hdlc_parser.OnFrameReady([&](const uint8_t *cleanBuffer, size_t cleanIdx)
                             {
        if (cleanIdx <= 2) return;
        
        size_t payload_len = cleanIdx - 2; // Отрезаем 2 байта CRC16 с хвоста пакета
        uint8_t cmd_code = cleanBuffer[0];

        // Пропускаем системные подтверждения команд модема
        if (cmd_code == 0x73 || cmd_code == 0x7D || cmd_code == 0x1C) {
            return;
        }

        // ЖЕСТКАЯ ХИРУРГИЯ ДОНОРА (ОРИГИНАЛ ОДИН В ОДИН):
        if (payload_len > 14)
        {
            uint8_t dnr_buffer[bufSize];
            std::memcpy(dnr_buffer, cleanBuffer, payload_len);

            uint8_t *log_record_ptr = dnr_buffer;
            size_t inner_len = payload_len - 10;

            // Каноничные сдвиги указателей донора, под которые заточен весь парсер!
            log_record_ptr += 2;
            log_record_ptr += 2;

            if (inner_len > 6)
            {
                std::memmove(log_record_ptr + 2, log_record_ptr + 4, inner_len - 6);
                inner_len -= 2; 
            }

            // В оригинальной структуре после всех сдвигов донора настоящий Log Code 
            // лежит строго на нулевом и первом байтах текущего положения указателя!
            uint16_t real_log_code = log_record_ptr[0] | (log_record_ptr[1] << 8);

            // Фильтруем вывод в консоль, чтобы видеть только SIB1 паспорта БС (0xB0C0)
            if (real_log_code == 0xB0C0 || real_log_code == 0xB0C1 || real_log_code == 0xB0C2 || real_log_code == 0xB0CD)
            {
                std::printf("[RADIO LOG] Пакет адаптирован! Источник USB: 0x%02X | Настоящий LogCode: 0x%04X | Длина: %zu\n", 
                            cmd_code, real_log_code, inner_len);
                std::fflush(stdout);
            }

            // Скармливаем ЧИСТУЮ структуру в оригинальный парсер андроидчика
            qcom_parser.on_log(log_record_ptr, inner_len);

            // Авторитарный вывод соты напрямую из lte_rrc в обход merge_lte_identity
            auto rrc_identity = qcom_parser.lte_rrc().identity();
            if (rrc_identity.valid)
            {
                std::printf("\n==================== STREAM LIVE MONITOR ====================\n");
                std::printf("[LTE Cell Live] CellID: %-10d | TAC: %-5u | PLMN: %03u-%02u | PCI: %-3u | EARFCN: %-5u\n", 
                            rrc_identity.cell_id, rrc_identity.tac, rrc_identity.mcc, rrc_identity.mnc, rrc_identity.pci, rrc_identity.earfcn);
                std::printf("-------------------------------------------------------------\n");
                std::fflush(stdout);
            }
        } });

    // 3. Создаем транспортный стрим поверх нашего последовательного порта
    SerialPortStream port_stream(SimPort);

    // Подключаем транспортный стрим к нашему конвейеру сериализатора!
    port_stream.OnRawData([&](const uint8_t *data, size_t size)
                          { hdlc_parser.ParseChunk(data, size); });

    // Запускаем асинхронный поток пакетной вычистки из очереди
    port_stream.Start();
    std::cout << "[Scanner] Пайплайн успешно запущен. Ожидание пакетов...\n";

    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n[Scanner] Завершение работы, остановка конвейеров...\n";
    port_stream.Stop();
    closeConnection = true;
    if (t0.joinable())
        t0.join();
    SimPort.CloseConnection();

    return 0;
}
