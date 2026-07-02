#include "hdlc_serializer.h"
#include "crc16_generic.h"
#include "serial_port.h"// Нужен для SerialPort::CreateCRC16
#include <cstring>

std::vector<uint8_t> HdlcSerializer::Serialize(uint8_t opcode, const uint8_t *payload, size_t payload_len, const HdlcConfig &cfg) {
    std::vector<uint8_t> raw_buf;
    raw_buf.reserve(1 + payload_len + 2);

    raw_buf.push_back(opcode);
    if (payload && payload_len > 0) {
        raw_buf.insert(raw_buf.end(), payload, payload + payload_len);
    }

    // ВЫЗОВ: Используем наш сгенерированный шаблон QualcommCrc
    uint16_t crc = QualcommCrc::Calculate(raw_buf.data(), raw_buf.size());

    raw_buf.push_back(crc & 0xFF);
    raw_buf.push_back((crc >> 8) & 0xFF);

    std::vector<uint8_t> tx_buf;
    tx_buf.reserve(raw_buf.size() * 2);

    for (uint8_t byte: raw_buf) {
        if (byte == cfg.escape_flag || byte == cfg.frame_flag) {
            tx_buf.push_back(cfg.escape_flag);
            tx_buf.push_back(byte ^ cfg.escape_xor);
        } else {
            tx_buf.push_back(byte);
        }
    }
    tx_buf.push_back(cfg.frame_flag);
    return tx_buf;
}

bool HdlcSerializer::DeserializeByte(uint8_t byte, uint8_t *clean_buf, size_t &clean_idx, size_t max_size,
                                     bool &inside_packet, bool &escape_next, const HdlcConfig &cfg) {
    if (byte == cfg.frame_flag) {
        if (!inside_packet) {
            inside_packet = true;
            clean_idx = 0;
            escape_next = false;
            return false;
        } else {
            inside_packet = false;
            return true;
        }
    }

    if (inside_packet && clean_idx < max_size) {
        if (byte == cfg.escape_flag) {
            escape_next = true;
        } else {
            if (escape_next) {
                clean_buf[clean_idx++] = byte ^ cfg.escape_xor;
                escape_next = false;
            } else {
                clean_buf[clean_idx++] = byte;
            }
        }
    }

    return false;
}
