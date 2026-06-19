#ifndef GENERIC_CRC16_H
#define GENERIC_CRC16_H

#include <cstdint>
#include <cstddef>
#include <array>

// Универсальный шаблон для ЛЮБОГО CRC-16 в мире
template <uint16_t Poly, uint16_t Init, bool ReflectIn, bool ReflectOut, uint16_t XorOut>
class GenericCrc16
{
public:
    static uint16_t Calculate(const uint8_t *data, size_t len)
    {
        // Таблица сгенерируется ОДИН раз при первом вызове программы
        static const auto table = GenerateTable();

        uint16_t crc = Init;
        for (size_t i = 0; i < len; ++i)
        {
            uint8_t byte = data[i];

            // Если нужен реверс входящих бит (как у Qualcomm)
            if constexpr (ReflectIn)
            {
                byte = Reflect8(byte);
            }

            crc = (crc << 8) ^ table[((crc >> 8) ^ byte) & 0xFF];
        }

        // Если нужен реверс исходящих бит
        if constexpr (ReflectOut)
        {
            crc = Reflect16(crc);
        }

        return crc ^ XorOut;
    }

private:
    // Разворот 8 бит
    static constexpr uint8_t Reflect8(uint8_t val)
    {
        val = ((val & 0xF0) >> 4) | ((val & 0x0F) << 4);
        val = ((val & 0xCC) >> 2) | ((val & 0x33) << 2);
        val = ((val & 0xAA) >> 1) | ((val & 0x55) << 1);
        return val;
    }

    // Разворот 16 бит
    static constexpr uint16_t Reflect16(uint16_t val)
    {
        uint16_t res = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (val & (1 << i))
            {
                res |= (1 << (15 - i));
            }
        }
        return res;
    }

    // Магия: генерация таблицы под заданный полином на лету
    static constexpr std::array<uint16_t, 256> GenerateTable()
    {
        std::array<uint16_t, 256> table{};
        for (int byte = 0; byte < 256; ++byte)
        {
            uint16_t crc = (byte << 8);
            for (int bit = 0; bit < 8; ++bit)
            {
                if (crc & 0x8000)
                {
                    crc = (crc << 1) ^ Poly;
                }
                else
                {
                    crc <<= 1;
                }
            }
            table[byte] = crc;
        }
        return table;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// АЛИАСЫ (Красивые имена для разных стандартов)
// ─────────────────────────────────────────────────────────────────────────────

// Твой каноничный Qualcomm DIAG (он же инвертированный CCITT)
using QualcommCrc = GenericCrc16<0x1021, 0xFFFF, true, true, 0xFFFF>;

#endif // GENERIC_CRC16_H
