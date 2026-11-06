// Юнит-тесты M3: декодер (порт CLTACompressedFile::ReadByte).
//
// Проверяем три вещи: (1) векторы M2 читаются обратно в исходные байты,
// (2) перекрывающийся span (классический приём LZSS) разворачивается как в оригинале,
// (3) обрезанный поток даёт префикс и не выходит за границы массива.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ltc/bits.h"
#include "ltc/compressed_stream.h"
#include "ltc/lzss_decoder.h"

namespace {

int g_failures = 0;
int g_checks   = 0;

#define CHECK(cond)                                                                   \
    do                                                                                \
    {                                                                                 \
        ++g_checks;                                                                   \
        if (!(cond))                                                                  \
        {                                                                             \
            ++g_failures;                                                             \
            std::cerr << "CHECK failed: " << #cond << "  (" << __FILE__ << ":"        \
                      << __LINE__ << ")\n";                                           \
        }                                                                             \
    } while (false)

#define CHECK_EQ(a, b)                                                                \
    do                                                                                \
    {                                                                                 \
        ++g_checks;                                                                   \
        const auto va = (a);                                                          \
        const auto vb = (b);                                                          \
        if (!(va == vb))                                                              \
        {                                                                             \
            ++g_failures;                                                             \
            std::cerr << "CHECK_EQ failed: " << #a << " == " << #b << "  ("           \
                      << __FILE__ << ":" << __LINE__ << ")\n";                        \
        }                                                                             \
    } while (false)

std::string read_file(const std::string& path)
{
    std::string out;
    std::FILE*  f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
    {
        return out;
    }
    char        buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
    {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

void write_file(const std::string& path, const std::string& data)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        return;
    }
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

std::filesystem::path make_temp_dir()
{
    const auto dir = std::filesystem::temp_directory_path() / "ltc_m3_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string to_bytes(const std::vector<std::uint8_t>& input)
{
    return std::string(input.begin(), input.end());
}

// сжатие тем же путём, что у CLI
std::string compress(const std::filesystem::path& dir, const std::vector<std::uint8_t>& input)
{
    const std::string path = (dir / "c.ltc").string();
    {
        ltc::CompressedWriter writer;
        if (!writer.open(path.c_str()))
        {
            return "<open failed>";
        }
        for (std::uint8_t byte : input)
        {
            if (!writer.write_byte(byte))
            {
                return "<write failed>";
            }
        }
        writer.close();
    }
    return read_file(path);
}

// поток -> байты (через CompressedReader, как в CLI)
std::string decompress(const std::filesystem::path& dir, const std::string& stream)
{
    const std::string path = (dir / "d.ltc").string();
    write_file(path, stream);

    ltc::CompressedReader reader;
    if (reader.open(path.c_str()) != ltc::CompressedOpen::kOk)
    {
        return "<open failed>";
    }
    std::string out;
    std::uint8_t value = 0;
    while (reader.read_byte(value))
    {
        out.push_back(static_cast<char>(value));
    }
    reader.close();
    return out;
}

//---------------------------------------------------------------------------
// Независимая упаковка потока (для ручных случаев)
//---------------------------------------------------------------------------
class Packer
{
public:
    Packer()
    {
        field(ltc::kCurrFileVersion, 32);   // заголовок: 32 бита версии
    }

    void literal(std::uint8_t value)
    {
        bit(1);
        field(value, 8);
    }

    void span(std::uint32_t offset, std::uint32_t length)
    {
        bit(0);
        field(offset, ltc::kNumOffsetBits);
        field(length - (ltc::kBreakEvenPoint + 1), ltc::kNumLengthBits);
    }

    void eos()
    {
        bit(0);
        field(0, ltc::kNumOffsetBits);
    }

    std::string finish() const { return m_Out; }

private:
    void bit(unsigned value)
    {
        if (value)
        {
            m_Curr = static_cast<std::uint8_t>(m_Curr | (1u << m_Count));
        }
        if (++m_Count == 8)
        {
            m_Out.push_back(static_cast<char>(m_Curr));
            m_Curr  = 0;
            m_Count = 0;
        }
    }

    void field(std::uint32_t value, unsigned bits)
    {
        for (unsigned i = 1; i <= bits; ++i)
        {
            bit((value >> (bits - i)) & 1u);
        }
    }

    std::string  m_Out;
    std::uint8_t m_Curr  = 0;
    unsigned     m_Count = 0;
};

//---------------------------------------------------------------------------

void test_vectors_roundtrip()
{
    const auto dir = make_temp_dir();

    struct Vector
    {
        const char* m_LtcHex;
        const char* m_Expected;
    };

    const Vector vectors[] = {
        {"000000000000", ""},
        {"00000000050100", "A"},
        {"00000000050b0100", "AB"},
        {"00000000050b162c0800", "AAAA"},
        {"00000000050b162c58b060c182050b162c58b060c182050100", "AAAAAAAAAAAAAAAAA"},
        {"00000000050b162c58b060c182050b162c58b060c182050b0200", "AAAAAAAAAAAAAAAAAA"},
        {"000000000d1b350680df1800", "abcabcabcabcabcabcabc"},
        {"000000000501e0172c58b060c182050b162c58b02000", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
    };

    for (const Vector& vec : vectors)
    {
        const std::string hex(vec.m_LtcHex);
        std::string       stream;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            const auto hi = static_cast<unsigned>(std::strchr("0123456789abcdef", hex[i]) - "0123456789abcdef");
            const auto lo = static_cast<unsigned>(std::strchr("0123456789abcdef", hex[i + 1]) - "0123456789abcdef");
            stream.push_back(static_cast<char>((hi << 4) | lo));
        }
        CHECK_EQ(decompress(dir, stream), std::string(vec.m_Expected));
    }

    std::filesystem::remove_all(dir);
}

void test_overlapping_span()
{
    const auto dir = make_temp_dir();

    // литерал 'A', затем span offset=1 (позиция окна, куда лёг литерал) длиной 17:
    // классическое перекрытие, span читает то, что сам же и записал
    Packer packer;
    packer.literal('A');
    packer.span(1, 17);
    packer.eos();
    CHECK_EQ(decompress(dir, packer.finish()), std::string(18, 'A'));

    // два литерала подряд и span на первый из них
    Packer second;
    second.literal('x');
    second.literal('y');
    second.span(1, 4);
    second.eos();
    CHECK_EQ(decompress(dir, second.finish()), std::string("xyxyxy"));

    std::filesystem::remove_all(dir);
}

void test_roundtrip_generated()
{
    const auto dir = make_temp_dir();

    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> cases;

    cases.emplace_back("empty", std::vector<std::uint8_t>{});
    cases.emplace_back("one", std::vector<std::uint8_t>{0x00});
    cases.emplace_back("two", std::vector<std::uint8_t>{'A', 'B'});

    std::vector<std::uint8_t> run(9000, 'Z');
    cases.emplace_back("run9000", run);

    std::vector<std::uint8_t> ramp(4096);
    for (int i = 0; i < 4096; ++i)
    {
        ramp[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((i * 37) & 0xFF);
    }
    cases.emplace_back("ramp4096", ramp);

    std::vector<std::uint8_t> window_cross(9000);
    for (std::size_t i = 0; i < window_cross.size(); ++i)
    {
        window_cross[i] = static_cast<std::uint8_t>((i / 7) & 0x0F);   // переживает окно
    }
    cases.emplace_back("window_cross", window_cross);

    for (const auto& [name, bytes] : cases)
    {
        const std::string packed = compress(dir, bytes);
        CHECK_EQ(decompress(dir, packed), to_bytes(bytes));
        (void)name;
    }

    std::filesystem::remove_all(dir);
}

void test_truncated_streams()
{
    const auto dir = make_temp_dir();

    // исходник, который эталон тоже сжимает «внятно»
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 400; ++i)
    {
        const char* chunk = "the quick brown fox jumps over the lazy dog. ";
        data.insert(data.end(), chunk, chunk + std::strlen(chunk));
    }

    const std::string packed = compress(dir, data);
    const std::string original = to_bytes(data);

    // обрезаем поток в разных местах: декодер обязан выдать префикс и не упасть
    for (std::size_t cut : {std::size_t(5), packed.size() / 2, packed.size() - 1})
    {
        if (cut == 0 || cut > packed.size())
        {
            continue;
        }
        const std::string truncated   = packed.substr(0, cut);
        const std::string recovered   = decompress(dir, truncated);
        CHECK(recovered.size() <= original.size());
        CHECK_EQ(recovered, original.substr(0, recovered.size()));
        CHECK(recovered.size() > 0);
    }

    std::filesystem::remove_all(dir);
}

} // namespace

int main()
{
    test_vectors_roundtrip();
    test_overlapping_span();
    test_roundtrip_generated();
    test_truncated_streams();

    std::cout << "checks: " << g_checks << ", failures: " << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
