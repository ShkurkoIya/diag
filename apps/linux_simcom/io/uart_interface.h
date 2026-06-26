#pragma once
#ifndef UART_INTERFACE_H
#define UART_INTERFACE_H

#include <cstdint>
#include <cstdio>
#include <sys/types.h>

class UartInterface {
public:
    virtual ~UartInterface() = default;

    // Инициализация и проверка состояния порта
    virtual bool Init() = 0;
    virtual bool DataAvail() = 0;
    virtual void CloseConnection() = 0;

    // Отправка и прием данных
    virtual bool SendData(uint8_t *dta, uint32_t size, bool async = false) = 0;
    virtual uint32_t ReadData(uint8_t *bufferPtr) = 0;
    virtual uint8_t GetByte(bool &res) = 0;

    // Служебные методы
    virtual uint32_t RichmondGetDataSize() { return 6144; }// Дефолтный размер буфера
    virtual uint32_t GetDataSize() = 0;
    virtual void Flush() = 0;

    // Потокобезопасность
    virtual bool LockMutex() = 0;
    virtual void UnlockMutex() = 0;
    virtual void DelaySim(uint64_t ms) = 0;

    // Виртуальные заглушки для файлов (возвращают nullptr, чтобы убрать файловую логику)
    virtual FILE *get_store_file() { return nullptr; }
    virtual FILE *get_cell_json_file() { return nullptr; }
};

#endif// UART_INTERFACE_H
