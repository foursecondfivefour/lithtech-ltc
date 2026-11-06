// Файловые потоки порта: несжатое чтение/запись байт.
//
// Оригинал ходит через CLTAFileBuffer (кэш 1024 байта, fopen "rb"/"wb"/"ab+"). Здесь
// то же самое по семантике: fopen в тех же режимах, побайтовые read/write, ошибка
// записи возвращается вызывающему (в оригинале — "Disk may be full.").
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace ltc {

class RawReader
{
public:
    ~RawReader();

    bool open(const char* path);
    bool is_valid() const { return m_File != nullptr; }
    bool read_byte(std::uint8_t& value);
    void close();

private:
    std::FILE* m_File = nullptr;
};

class RawWriter
{
public:
    ~RawWriter();

    bool open(const char* path);
    bool is_valid() const { return m_File != nullptr; }
    bool write_byte(std::uint8_t value);
    bool close();

private:
    std::FILE* m_File = nullptr;
};

} // namespace ltc
