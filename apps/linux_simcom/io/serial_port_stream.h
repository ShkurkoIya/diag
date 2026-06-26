#ifndef SERIAL_PORT_STREAM_H
#define SERIAL_PORT_STREAM_H

#include "data_stream_interface.h"
#include "serial_port.h"
#include <atomic>
#include <thread>
#include <vector>

class SerialPortStream : public IDataStream {
public:
    SerialPortStream(SerialPort &port, size_t chunk_size = 6144)
        : m_port(port), m_chunk_size(chunk_size), m_running(false) {}

    ~SerialPortStream() { Stop(); }

    void OnRawData(DataCallback cb) override { m_callback = std::move(cb); }

    void Start() {
        if (m_running)
            return;
        m_running = true;
        m_thread = std::thread(&SerialPortStream::WorkerThread, this);
    }

    void Stop() {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    void WorkerThread() {
        std::vector<uint8_t> buffer(m_chunk_size, 0);
        while (m_running) {
            int bytes_read = m_port.ReadDataFromQueue(buffer.data(), m_chunk_size);

            if (bytes_read > 0) {
                // !!! ДОБАВЛЯЕМ ЭТОТ ПРИНТ ДЛЯ ТЕСТА:
                std::printf("[STREAM] Считали из очереди порта: %d байт\n", bytes_read);
                std::fflush(stdout);

                if (m_callback) {
                    m_callback(buffer.data(), bytes_read);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    SerialPort &m_port;
    size_t m_chunk_size;
    std::atomic<bool> m_running;
    std::thread m_thread;
    DataCallback m_callback;
};

#endif// SERIAL_PORT_STREAM_H