#include "ltc/compressed_stream.h"

#include <cstring>

namespace ltc {

//---------------------------------------------------------------------------
// CompressedWriter
//---------------------------------------------------------------------------

CompressedWriter::~CompressedWriter()
{
    close();
}

bool CompressedWriter::open(const char* path)
{
    close();

    m_File = std::fopen(path, "wb");
    if (m_File == nullptr)
    {
        return false;
    }

    m_Bits = new BitWriter(m_File);

    // ltacompressedfile.cpp: заголовок — 32 бита версии, старшим битом вперёд,
    // затем инициализация окна
    m_Bits->write_field(kCurrFileVersion, 32);
    m_Window.init();
    return true;
}

bool CompressedWriter::write_byte(std::uint8_t value)
{
    if (m_Bits == nullptr)
    {
        return false;
    }

    // CLTACompressedFile::WriteByte -> CLZSSWindow::AddByte
    return m_Window.add_byte(value, *m_Bits) && m_Bits->ok();
}

bool CompressedWriter::close()
{
    if (m_Bits == nullptr)
    {
        if (m_File != nullptr)
        {
            std::fclose(m_File);
            m_File = nullptr;
        }
        return true;
    }

    // CLTACompressedFile::Close: досбросить look-ahead (это же пишет EOS), затем
    // закрыть битовый поток (Q2: неполный байт при этом отбрасывается)
    const bool flushed = m_Window.flush_look_ahead(*m_Bits);
    bool ok = m_Bits->close() && flushed;
    delete m_Bits;
    m_Bits = nullptr;

    if (m_File != nullptr)
    {
        ok = (std::fflush(m_File) == 0) && ok;
        std::fclose(m_File);
        m_File = nullptr;
    }
    return ok;
}

//---------------------------------------------------------------------------
// CompressedReader (M1: только заголовок)
//---------------------------------------------------------------------------

CompressedReader::~CompressedReader()
{
    close();
}

CompressedOpen CompressedReader::open(const char* path)
{
    close();

    m_Source = new RawReader();
    if (!m_Source->open(path))
    {
        close();
        return CompressedOpen::kOpenFailed;
    }

    m_Bits = new BitReader(*m_Source);

    // Та же последовательность, что в CLTACompressedFile::Open(READ):
    //   nCurrVersion <<= 1; if (bit) nCurrVersion |= (1 << nCurrBit);
    // Из-за сдвига любой ненулевой бит уезжает в 31-й разряд, поэтому проверка
    // сводится к «все 32 бита нулевые».
    std::uint32_t version = 0;
    for (unsigned bit = 0; bit < 32; ++bit)
    {
        bool value = false;
        m_Bits->get_bit(value);
        version <<= 1;
        if (value)
        {
            version |= (1u << bit);
        }
    }

    if (version != kCurrFileVersion)
    {
        close();
        return CompressedOpen::kOpenFailed;
    }

    // CLTACompressedFile::Open(READ): m_nDecSpanLen = 0; m_nDecWndPos = 1;
    m_Decoder = new LzssDecoder();
    return CompressedOpen::kOk;
}

bool CompressedReader::read_byte(std::uint8_t& value)
{
    if (m_Decoder == nullptr || m_Bits == nullptr)
    {
        return false;
    }
    return m_Decoder->read_byte(value, *m_Bits);
}

void CompressedReader::close()
{
    delete m_Decoder;
    m_Decoder = nullptr;
    delete m_Bits;
    m_Bits = nullptr;
    if (m_Source != nullptr)
    {
        m_Source->close();
        delete m_Source;
        m_Source = nullptr;
    }
}

} // namespace ltc
