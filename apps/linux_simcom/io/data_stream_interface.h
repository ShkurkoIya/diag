#ifndef DATA_STREAM_INTERFACE_H
#define DATA_STREAM_INTERFACE_H

#include <cstddef>
#include <cstdint>
#include <functional>

class IDataStream {
public:
    virtual ~IDataStream() = default;

    using DataCallback = std::function<void(const uint8_t *data, size_t size)>;

    virtual void OnRawData(DataCallback cb) = 0;
};

#endif// DATA_STREAM_INTERFACE_H