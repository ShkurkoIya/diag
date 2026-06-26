#include "ce_thread.h"
#include <chrono>
#include <iostream>
#include <thread>

void CeMain(SerialPort &port, bool &closeConnection) {
    std::cout << "[CeThread] Фоновый поток чтения DIAG запущен." << std::endl;

    while (!closeConnection) {
        bool has_data = port.ReadToRX();

        if (!has_data) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::cout << "[CeThread] Фоновый поток чтения Diag остановлен." << std::endl;
}
