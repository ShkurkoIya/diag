#ifndef CESERIAL_H
#define CESERIAL_H

#include <mutex>
#include <string>

class ceSerial {
public:
    ceSerial(std::string device, long baud, uint32_t db, char parity, uint32_t sb);
    ~ceSerial();

    bool Open();
    void Close();
    bool Write(uint8_t *data, uint32_t size);
    int Read(uint8_t *buffer, uint32_t size);
    bool IsOpen() const { return fd != -1; }

private:
    std::string port;
    long baudRate;
    uint32_t dataBits;
    char parityBit;
    uint32_t stopBits;
    int fd;
};

#endif