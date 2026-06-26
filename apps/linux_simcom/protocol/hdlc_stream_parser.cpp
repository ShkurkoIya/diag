#include "hdlc_stream_parser.h"

HdlcStreamParser::HdlcStreamParser(HdlcConfig config, size_t max_frame_size)
    : m_config(config) {
    m_clean_buf.resize(max_frame_size, 0);
}

void HdlcStreamParser::ParseChunk(const uint8_t *chunk, size_t size) {
    if (!chunk || size == 0)
        return;

    for (size_t i = 0; i < size; ++i) {
        if (HdlcSerializer::DeserializeByte(chunk[i], m_clean_buf.data(), m_clean_idx, m_clean_buf.size(),
                                            m_inside_packet, m_escape_next, m_config)) {
            if (m_callback && m_clean_idx > 0) {
                m_callback(m_clean_buf.data(), m_clean_idx);
            }
            m_clean_idx = 0;
        }
    }
}
