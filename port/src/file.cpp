#include "ltc/file.h"

namespace ltc {

RawReader::~RawReader()
{
    close();
}

bool RawReader::open(const char* path)
{
    close();
    // ltafilebuffer.cpp: OPEN_READ -> "rb" (и, как следствие, каталог тоже "открывается")
    m_File = std::fopen(path, "rb");
    return m_File != nullptr;
}

bool RawReader::read_byte(std::uint8_t& value)
{
    if (m_File == nullptr)
    {
        return false;
    }
    return std::fread(&value, 1, 1, m_File) == 1;
}

void RawReader::close()
{
    if (m_File != nullptr)
    {
        std::fclose(m_File);
        m_File = nullptr;
    }
}

RawWriter::~RawWriter()
{
    close();
}

bool RawWriter::open(const char* path)
{
    close();
    // OPEN_WRITE, без append -> "wb" (существующий файл усекается — Q4)
    m_File = std::fopen(path, "wb");
    return m_File != nullptr;
}

bool RawWriter::write_byte(std::uint8_t value)
{
    if (m_File == nullptr)
    {
        return false;
    }
    return std::fwrite(&value, 1, 1, m_File) == 1;
}

bool RawWriter::close()
{
    if (m_File == nullptr)
    {
        return true;
    }
    const bool failed = std::fflush(m_File) != 0;
    std::fclose(m_File);
    m_File = nullptr;
    return !failed;
}

} // namespace ltc
