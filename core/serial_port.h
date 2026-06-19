#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <string>
#include <mutex>
#include <cstdint>
#include <thread>
#include <sys/types.h>

#include "ce_serial.h"
#include "uart_interface.h"

// Размер буфера обмена (6 КБ)
#define bufSize 6144

class SerialPort : public UartInterface
{
public:
    explicit SerialPort(std::string &path);
    ~SerialPort() override;

    // Базовое управление железкой (Интерфейс UartInterface)
    bool Init() override;
    bool DataAvail() override;
    void CloseConnection() override;
    bool SendData(uint8_t *dta, uint32_t size, bool async = false) override;

    // Главные асинхронные методы для нашего реактивного пайплайна
    int ReadDataFromQueue(uint8_t *outBuffer, size_t maxLen); // Main Thread (через SerialPortStream)
    bool ReadToRX();                                          // Фоновый CeThread (через CeMain)

    // Заглушки легаси-интерфейса UartInterface (не ломают сборку, возвращают дефолт)
    uint8_t GetByte(bool &res) override
    {
        res = false;
        return 0;
    }
    uint32_t GetDataSize() override { return bufSize; }
    uint32_t ReadData(uint8_t *bufferPtr) override
    {
        (void)bufferPtr;
        return 0;
    }
    void Flush() override {}
    bool LockMutex() override { return true; }
    void UnlockMutex() override {}
    void DelaySim(uint64_t ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    FILE *get_store_file() override { return nullptr; }
    FILE *get_cell_json_file() override { return nullptr; }

private:
    std::string sPortName;
    ceSerial *ce_uart = nullptr;
    std::mutex portMutex; // Защита отправки SendData

    // Асинхронная кольцевая очередь
    uint8_t rxQueueBuffer[bufSize];
    std::mutex queueMutex; // Защита индексов при чтении/записи
    size_t head = 0;       // Сюда пишет фоновый поток ReadToRX
    size_t tail = 0;       // Отсюда читает основной поток ReadDataFromQueue
};

#endif // SERIALPORT_H
