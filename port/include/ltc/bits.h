// Порт CLTABitFile (сторона записи).
//
// Биты укладываются в байт от младшего к старшему — как `m_nCurrData |= (1 << m_nCurrBit)`
// в оригинале. Многобитные поля пишутся старшим битом вперёд (`write_field`).
//
// Q2: Close() НЕ дописывает неполный байт. Оригинал этого не делает, и именно поэтому
// пустой файл занимает 6 байт, а не 7 (32 бита версии + 17 бит EOS = 49 бит -> 6 байт).
#pragma once

#include <cstdint>
#include <cstdio>

#include "ltc/file.h"

namespace ltc {

class BitWriter
{
public:
    explicit BitWriter(std::FILE* file) : m_File(file) {}

    // один бит, порядок внутри байта — от младшего
    void set_bit(unsigned bit)
    {
        if (bit & 1u)
        {
            m_CurrData = static_cast<uint8_t>(m_CurrData | (1u << m_CurrBit));
        }
        if (++m_CurrBit == 8)
        {
            flush_byte();
        }
    }

    // многобитное поле, старшим битом вперёд
    void write_field(uint32_t value, unsigned bits)
    {
        for (unsigned i = 1; i <= bits; ++i)
        {
            set_bit((value >> (bits - i)) & 1u);
        }
    }

    // отбрасывает неполный байт (Q2) и сообщает, не было ли ошибок записи
    bool close()
    {
        m_CurrData = 0;
        m_CurrBit  = 0;
        return m_Ok;
    }

    bool ok() const { return m_Ok; }

private:
    void flush_byte()
    {
        if (std::fputc(m_CurrData, m_File) == EOF)
        {
            m_Ok = false;
        }
        m_CurrData = 0;
        m_CurrBit  = 0;
    }

    std::FILE* m_File;
    uint8_t    m_CurrData = 0;
    unsigned   m_CurrBit  = 0;
    bool       m_Ok       = true;
};


// Порт CLTABitFile (сторона чтения).
//
// Q10: в оригинале `GetBit` при конце файла возвращает false и НЕ трогает выходной
// параметр — вызывающий код читает неинициализированную переменную (UB). Порт
// определяет это как «бит = 0»: для проверки версии это даёт ровно то поведение,
// которое наблюдается у эталона на обрезанных файлах (0..8 нулевых байт — валидны,
// любой ненулевой бит версии — невалиден).

// Константы формата (ltacompressedfile.cpp / lzsslimits.h)
inline constexpr unsigned kNumOffsetBits = 12;
inline constexpr unsigned kNumLengthBits = 4;
inline constexpr unsigned kWindowSize    = 1u << kNumOffsetBits;                 // 4096
inline constexpr unsigned kBreakEvenPoint =
    (1 + kNumOffsetBits + kNumLengthBits) / 9;                                   // == 1
inline constexpr unsigned kLookAheadSize = (1u << kNumLengthBits) + 1;           // 17
inline constexpr uint32_t kCurrFileVersion = 0;

class BitReader
{
public:
    explicit BitReader(RawReader& source) : m_Source(source) {}

    // false — данных больше нет (бит считается нулевым)
    bool get_bit(bool& bit)
    {
        if (m_Mask == 0x00)
        {
            std::uint8_t data = 0;
            if (!m_Source.read_byte(data))
            {
                bit = false;
                m_Eof = true;
                return false;
            }
            m_CurrData = data;
            m_Mask     = 0x01;
        }

        bit    = (m_CurrData & m_Mask) != 0;
        m_Mask = static_cast<std::uint8_t>(m_Mask << 1);
        return true;
    }

    // многобитное поле, старшим битом вперёд; недостающие биты — нули
    std::uint32_t read_field(unsigned bits)
    {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < bits; ++i)
        {
            bool bit = false;
            get_bit(bit);
            value = (value << 1) | (bit ? 1u : 0u);
        }
        return value;
    }

    bool eof() const { return m_Eof; }

private:
    RawReader&   m_Source;
    std::uint8_t m_CurrData = 0;
    std::uint8_t m_Mask     = 0x00;
    bool         m_Eof      = false;
};

} // namespace ltc
