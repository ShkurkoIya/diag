#include "serial_port.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

SerialPort::SerialPort(std::string &path)
    : sPortName(path), head(0), tail(0)
{
    // Конструктор чист: никаких fopen файлов дампа и скрытых путей в /tmp
}

SerialPort::~SerialPort()
{
    CloseConnection();
}

bool SerialPort::Init()
{
    ce_uart = new ceSerial(sPortName, 115200, 8, 'N', 1);
    // Просто возвращаем то, что вернул ceSerial (true/1 при успехе)
    return ce_uart->Open();
}

void SerialPort::CloseConnection()
{
    if (ce_uart)
    {
        ce_uart->Close();
        delete ce_uart;
        ce_uart = nullptr;
    }
}

bool SerialPort::DataAvail()
{
    return ce_uart && ce_uart->IsOpen();
}

bool SerialPort::SendData(uint8_t *dta, uint32_t size, bool async)
{
    (void)async;
    if (!ce_uart || !ce_uart->IsOpen() || !dta || size == 0)
        return false;

    // Мьютекс гарантирует, что пакеты команд улетят в модем целиком и не перемешаются
    std::lock_guard<std::mutex> lock(portMutex);
    return ce_uart->Write(dta, size);
}

// ─────────────────────────────────────────────────────────────────────────────
// РАБОЧИЙ КОНВЕЙЕР ОБМЕНА ДАННЫМИ (БЕЗ ГОНОК ПОТОКОВ)
// ─────────────────────────────────────────────────────────────────────────────

int SerialPort::ReadDataFromQueue(uint8_t *outBuffer, size_t maxLen)
{
    if (!outBuffer || maxLen == 0)
        return 0;

    std::lock_guard<std::mutex> lock(queueMutex);

    // Если хвост равен голове — очередь абсолютно пуста, читать нечего
    if (tail == head)
    {
        return 0;
    }

    size_t bytes_read = 0;

    // ИСПРАВЛЕНО: Выгребаем данные из rxQueueBuffer, двигая tail вперед по кольцу донора!
    while (tail != head && bytes_read < maxLen)
    {
        outBuffer[bytes_read++] = rxQueueBuffer[tail];
        tail = (tail + 1) % bufSize; // Двигаем хвост за головой
    }

    return static_cast<int>(bytes_read);
}

// Фоновый поток CeThread непрерывно крутит этот метод для забора байт из USB в кольцо
bool SerialPort::ReadToRX()
{
    if (!ce_uart || !ce_uart->IsOpen())
        return false;

    uint8_t tmp[256];

    // Вызов read() больше не вешает поток, а мгновенно забирает чанк, если он прилетел
    int read_bytes = ce_uart->Read(tmp, sizeof(tmp));

    // Если read_bytes == -1 и errno == EAGAIN — это норма для O_NONBLOCK (данных просто пока нет)
    if (read_bytes <= 0)
    {
        return false;
    }

    // Если данные есть, атомарно пишем их в наше кольцо донора
    std::lock_guard<std::mutex> lock(queueMutex);

    for (int i = 0; i < read_bytes; i++)
    {
        size_t next_head = (head + 1) % bufSize;

        if (next_head == tail)
        {
            break; // Защита от переполнения
        }

        rxQueueBuffer[head] = tmp[i];
        head = next_head;
    }
    return true;
}
