#ifndef CETHREAD_H
#define CETHREAD_H

#include "serial_port.h"

// Точка входа для std::thread фонового чтения
void CeMain(SerialPort &port, bool &closeConnection);

#endif // CETHREAD_H
