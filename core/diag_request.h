#pragma once
#ifndef DIAGREQUEST_H
#define DIAGREQUEST_H

#include <vector>
#include <cstdint>
#include <cstddef>
#include "serial_port.h"

class DiagRequest
{
public:
    explicit DiagRequest(SerialPort *port);
    ~DiagRequest() = default;

    // Запрещаем копирование объекта во избежание дублирования указателей
    DiagRequest(const DiagRequest &) = delete;
    DiagRequest &operator=(const DiagRequest &) = delete;

    /**
     * @brief Проверка доступности порта перед стартом
     */
    bool Init();

    /**
     * @brief Сброс масок и подавление флуда F3 по логике донора
     */
    bool ZeroLog();

    /**
     * @brief Полный запуск измерительного пайплайна ( LTE_CONFIG + GSM_CONFIG + COMMIT + STREAM ENABLE )
     */
    bool StartREO();

    /**
     * @brief Отправка кастомного пакета DIAG (автоматически вызывает HdlcSerializer и считает CRC)
     */
    bool SendDiagPacket(uint8_t opcode, const uint8_t *payload, size_t payload_len);

    // ─────────────────────────────────────────────────────────────────────────
    // МЕТОДЫ ДЛЯ СОВМЕСТИМОСТИ (Синхронизированы с cpp)
    // ─────────────────────────────────────────────────────────────────────────
    bool CommitLogs();
    bool DisableAllLogs() { return ZeroLog(); }
    bool DisableMessageFlood() { return true; }
    bool SetMask(uint32_t equip_id, size_t padding_bytes, size_t mask_bytes, const std::vector<uint16_t> &target_codes);
    int ReadCycle(uint8_t &buffer)
    {
        (void)buffer;
        return 0;
    }

private:
    SerialPort *m_port;
};

#endif // DIAGREQUEST_H
