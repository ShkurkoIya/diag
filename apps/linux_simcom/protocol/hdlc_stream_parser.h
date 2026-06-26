#ifndef HDLC_STREAM_PARSER_H
#define HDLC_STREAM_PARSER_H

#include "data_stream_interface.h"
#include "hdlc_serializer.h"

class HdlcStreamParser {
public:
    using FrameReadyCallback = std::function<void(const uint8_t *clean_buf, size_t clean_len)>;

    HdlcStreamParser(HdlcConfig config = HdlcConfig{}, size_t max_frame_size = 6144);
    ~HdlcStreamParser() = default;

    void OnFrameReady(FrameReadyCallback cb) { m_callback = std::move(cb); }
    void ParseChunk(const uint8_t *chunk, size_t size);

private:
    FrameReadyCallback m_callback;
    HdlcConfig m_config;
    std::vector<uint8_t> m_clean_buf;
    size_t m_clean_idx = 0;
    bool m_inside_packet = false;
    bool m_escape_next = false;
};

#endif// HDLC_STREAM_PARSER_H