#include "ce_serial.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

ceSerial::ceSerial(std::string device, long baud, uint32_t db, char parity, uint32_t sb)
    : port(device), baudRate(baud), dataBits(db), parityBit(parity), stopBits(sb), fd(-1) {
}

ceSerial::~ceSerial() { Close(); }

bool ceSerial::Open() {
    // ИСПРАВЛЕНО: Добавляем флаг O_NONBLOCK (Неблокирующее чтение/запись)
    fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
        printf("[ceSerial] КРИТИЧЕСКАЯ ОШИБКА open(): %s (errno: %d)\n", strerror(errno), errno);
        return false;
    }

    // ИСПРАВЛЕНО: Явно подтверждаем неблокирующий флаг на уровне дескриптора ядра
    fcntl(fd, F_SETFL, O_NONBLOCK);

    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        printf("[ceSerial] КРИТИЧЕСКАЯ ОШИБКА tcgetattr(): %s\n", strerror(errno));
        close(fd);
        fd = -1;
        return false;
    }

    cfmakeraw(&options);

    // Поднимаем скорость до 921600 бод (стандарт Qualcomm DIAG по USB/MHI)
    cfsetispeed(&options, B921600);
    cfsetospeed(&options, B921600);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // ИСПРАВЛЕНО: Переводим termios в режим мгновенного возврата управления!
    options.c_cc[VMIN] = 0; // Если в буфере пусто — не ждать ни наносекунды, возвращать 0
    options.c_cc[VTIME] = 0;// Нулевой таймаут

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        printf("[ceSerial] КРИТИЧЕСКАЯ ОШИБКА tcsetattr(): %s\n", strerror(errno));
        close(fd);
        fd = -1;
        return false;
    }

    // ... Оставляем твой жесткий подъем DTR/RTS без изменений, он написан отлично! ...
    return true;
}

void ceSerial::Close() {
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

bool ceSerial::Write(uint8_t *data, uint32_t size) {
    if (fd == -1 || !data || size == 0)
        return false;

    size_t total_written = 0;
    int retries = 0;

    // В неблокирующем режиме пишем в цикле, пока не протолкнем весь размер
    while (total_written < size && retries < 10) {
        ssize_t res = ::write(fd, data + total_written, size - total_written);

        if (res > 0) {
            total_written += res;
        } else if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Буфер ядра переполнен, спим 1 мс и пробуем снова
            retries++;
            usleep(1000);
        } else {
            // Произошла реальная аппаратная ошибка порта
            return false;
        }
    }

    return total_written == size;
}

int ceSerial::Read(unsigned char *buffer, uint32_t size) {
    if (fd == -1)
        return -1;
    return ::read(fd, buffer, size);
}
