// Потоки .ltc: запись (полноценная) и чтение (пока только проверка заголовка).
//
// Писатель: версия + CLZSSWindow (порт 1:1) + EOS — побайтовое совпадение с эталонным
// компрессором. Читатель: проверка заголовка + декодер (порт CLTACompressedFile::ReadByte).
#pragma once

#include <cstdint>

#include "ltc/bits.h"
#include "ltc/file.h"
#include "ltc/lzss_decoder.h"
#include "ltc/lzss_window.h"

namespace ltc {

class CompressedWriter
{
public:
    ~CompressedWriter();

    bool open(const char* path);
    bool is_valid() const { return m_File != nullptr; }
    bool write_byte(std::uint8_t value);
    bool close();

private:
    std::FILE*  m_File   = nullptr;
    BitWriter*  m_Bits   = nullptr;
    LzssWindow  m_Window;
};

enum class CompressedOpen
{
    kOk,           // поток открыт и версия валидна
    kOpenFailed    // нет файла / версия не та -> "Error: Unable to open the file X"
};

class CompressedReader
{
public:
    ~CompressedReader();

    CompressedOpen open(const char* path);
    bool          is_valid() const { return m_Bits != nullptr; }
    bool          read_byte(std::uint8_t& value);
    void          close();

private:
    RawReader*   m_Source  = nullptr;
    BitReader*   m_Bits    = nullptr;
    LzssDecoder* m_Decoder = nullptr;
};

} // namespace ltc
