#include "diag_request.h"
#include "hdlc_serializer.h" // Наш утилитарный HdlcSerializer
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

DiagRequest::DiagRequest(SerialPort *port) : m_port(port) {}

bool DiagRequest::Init()
{
    if (!m_port || !m_port->DataAvail())
        return false;
    std::cout << "[DiagRequest] Инициализация сессии по структурам донора..." << std::endl;
    return true;
}

// Потокобезопасная отправка через наш реактивный HdlcSerializer (считает QualcommCrc внутри)
bool DiagRequest::SendDiagPacket(uint8_t opcode, const uint8_t *payload, size_t payload_len)
{
    if (!m_port)
        return false;

    // Автоматически упаковываем пакет в HDLC фрейм с расчетом таблицы контрольной суммы
    std::vector<uint8_t> tx_packet = HdlcSerializer::Serialize(opcode, payload, payload_len);

    // Пуляем готовый массив байт в порт
    return m_port->SendData(tx_packet.data(), tx_packet.size());
}

// Его родной проверенный метод очистки масок и глушения флуда F3
bool DiagRequest::ZeroLog()
{
    std::cout << "[DiagRequest] Очистка масок логов донора (Команда 0x73)..." << std::endl;

    uint8_t zero_payload[100];
    std::memset(zero_payload, 0, sizeof(zero_payload));
    uint32_t op = 0;                       // LOG_CONFIG_DISABLE_OP
    std::memcpy(&zero_payload[3], &op, 4); // Смещение 3xI
    bool res1 = SendDiagPacket(0x73, zero_payload, 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cout << "[DiagRequest] Отключение отладочного флуда сообщений F3 (Команда 0x7D)..." << std::endl;
    uint8_t msg_payload[100];
    std::memset(msg_payload, 0, sizeof(msg_payload));
    msg_payload[0] = 4;                    // MSG_EXT_SUBCMD_SET_ALL_RT_MASKS
    uint32_t lvl = 0;                      // MSG_LVL_NONE
    std::memcpy(&msg_payload[3], &lvl, 4); // Смещение BxxI
    bool res2 = SendDiagPacket(0x7D, msg_payload, 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Тот самый критический пинок для активации радиомодуля SIMCom!
    std::cout << "[DiagRequest] Запрос поддерживаемых диапазонов (Активация радиомодуля)..." << std::endl;
    std::memset(zero_payload, 0, sizeof(zero_payload));
    uint32_t op_retrieve = 1; // LOG_CONFIG_RETRIEVE_ID_RANGES_OP
    std::memcpy(&zero_payload[3], &op_retrieve, 4);
    SendDiagPacket(0x73, zero_payload, 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    return res1 && res2;
}

// Его родной рабочий метод StartREO, который зажигает соту в прошивке SIM8300
bool DiagRequest::StartREO()
{
    std::cout << "[DiagRequest] Активация логов по проверенным структурам донора..." << std::endl;

    // Сначала тотально тушим дефолтный мусор через метод ZeroLog
    if (!ZeroLog())
        return false;

    uint8_t buffer[bufSize];
    uint32_t op_set = 3; // LOG_CONFIG_SET_MASK_OP

    // ─────────────────────────────────────────────────────────────────────────
    // 1. Отправка точной рабочей маски LTE_CONFIG ("3xIIBB26xIBB35x")
    // ─────────────────────────────────────────────────────────────────────────
    std::memset(buffer, 0, sizeof(buffer));
    uint32_t equip_lte = 0x0B; // Специфичный для SIMCom ID масок
    uint8_t b1 = 0x01;
    uint8_t b2 = 0x02;
    uint32_t b3 = 0x01;
    uint8_t b4 = 0x0C;
    uint8_t b5 = 0xFF;

    size_t idx = 3; // 3x отступ
    std::memcpy(&buffer[idx], &op_set, 4);
    idx += 4; // I
    std::memcpy(&buffer[idx], &equip_lte, 4);
    idx += 4;           // I
    buffer[idx++] = b1; // B
    buffer[idx++] = b2; // B

    std::memset(&buffer[idx], 0x00, 26); // 26x выравнивание
    idx += 26;

    std::memcpy(&buffer[idx], &b3, 4);
    idx += 4;           // I
    buffer[idx++] = b4; // B
    buffer[idx++] = b5; // B

    // Начало битовой карты маски LTE (offset 45)
    size_t lte_mask_start = idx;
    std::memset(&buffer[lte_mask_start], 0x00, 35); // 35 байт под карту логов

    // Включаем строго те коды, от которых оживает твой SIB1 парсер!
    buffer[lte_mask_start + 24] = 0x07; // Включает 0xB0C0, 0xB0C1, 0xB0C2 (Паспорта БС!)
    buffer[lte_mask_start + 25] = 0x20; // Включает 0xB0CD (Важная RRC сигнализация)
    buffer[lte_mask_start + 47] = 0x80; // Включает 0xB17F (Физика сигнала)
    buffer[lte_mask_start + 50] = 0x08; // Включает 0xB193 (Децибелы RSRP)
    buffer[lte_mask_start + 51] = 0x80; // Включает 0xB197

    idx += 35; // Финальный размер пакета строго 80 байт

    std::cout << "[DiagRequest] Отправка проверенной маски LTE (размер: " << idx << " байт)..." << std::endl;
    if (!SendDiagPacket(0x73, buffer, idx))
        return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ─────────────────────────────────────────────────────────────────────────
    // 2. Отправка точной рабочей маски GSM_CONFIG ("3xIIBB39xB30xB63x")
    // ─────────────────────────────────────────────────────────────────────────
    std::memset(buffer, 0, sizeof(buffer));
    uint32_t equip_gsm = 0x05;
    uint8_t g1 = ' ';
    uint8_t g2 = 0x04;
    uint8_t g3 = 0x80;
    uint8_t g4 = '@';

    idx = 3; // 3x
    std::memcpy(&buffer[idx], &op_set, 4);
    idx += 4; // I
    std::memcpy(&buffer[idx], &equip_gsm, 4);
    idx += 4;           // I
    buffer[idx++] = g1; // B
    buffer[idx++] = b2; // B (у донора тут жестко b2 из LTE блока)

    std::memset(&buffer[idx], 0x00, 39);
    idx += 39;          // 39x
    buffer[idx++] = g3; // B
    std::memset(&buffer[idx], 0x00, 30);
    idx += 30;          // 30x
    buffer[idx++] = g4; // B
    std::memset(&buffer[idx], 0x00, 63);
    idx += 63; // 63x

    std::cout << "[DiagRequest] Отправка проверенной маски GSM (размер: " << idx << " байт)..." << std::endl;
    if (!SendDiagPacket(0x73, buffer, idx))
        return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ─────────────────────────────────────────────────────────────────────────
    // 3. Отправка оригинальной команды RUNTIME COMMIT (Фиксация масок, Операция 1)
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "[DiagRequest] Отправка команды RUNTIME COMMIT..." << std::endl;
    std::memset(buffer, 0, sizeof(buffer));
    uint32_t op_start = 1;
    idx = 3;
    std::memcpy(&buffer[idx], &op_start, 4);

    return SendDiagPacket(0x73, buffer, 7);
}

// Заглушки легаси-интерфейса наружу (чтобы не ругался компилятор)
bool DiagRequest::SetMask(uint32_t equip_id, size_t padding_bytes, size_t mask_bytes, const std::vector<uint16_t> &target_codes)
{
    (void)equip_id;
    (void)padding_bytes;
    (void)mask_bytes;
    (void)target_codes;
    return true;
}
bool DiagRequest::CommitLogs() { return true; }
