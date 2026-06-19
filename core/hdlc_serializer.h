#ifndef HDLC_UTILS_H
#define HDLC_UTILS_H

#include <cstdint>
#include <cstddef>
#include <vector>

struct HdlcConfig
{
    uint8_t frame_flag = 0x7E;
    uint8_t escape_flag = 0x7D;
    uint8_t escape_xor = 0x20;
};

class HdlcSerializer
{
public:
    /**
     * @brief Капсуляция (Упаковка сырого пакета HDLC-фрейм для отправки по USB)
     * @param opcode Код команды (например, 0x73)
     * @param payload Полезная нагрузка
     * @param payload_len Длинна полезной нагрузки
     * @param std::vector<uint8_t> Готовый к отправке по TTY массив байт
     */
    static std::vector<uint8_t> Serialize(uint8_t opcode, const uint8_t *payload, size_t payload_len,
                                            const HdlcConfig &cfg = HdlcConfig{});

    /**
     * @brief Декапсуляция одного байта (Понабираем пакет по ходу чтения стрима)
     * @param byte Текущий считаный из порта байт
     * @param clean_buf Буфер, куда собирается очищенный пакет
     * @param clean_idx Текущий размер собранного пакета
     * @param max_size Максимальный размер буфера (bufSize)
     * @param inside_packet Флаг состояния: находимся ли мы внутри фрейма
     * @param escape_next Флаг состояния: нужно ли кросить следующий байт
     * @return true - Пакет полностю собран (достигли закрывающего 0x7E)
     */
    static bool DeserializeByte(uint8_t byte, uint8_t *clean_buf, size_t &clean_idx, size_t max_size,
                                bool &inside_packet, bool &escape_next, const HdlcConfig &cfg = HdlcConfig{});
};

#endif // HDLC_UTILS
