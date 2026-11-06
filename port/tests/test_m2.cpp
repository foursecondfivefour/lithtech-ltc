// Юнит-тесты M2: побайтовые векторы компрессора (сняты с эталона) и инварианты
// структуры потока. Тесты не требуют эталонного бинарника — гейт может их гонять
// где угодно, а сверка с эталоном живёт в scripts/m2_gate.sh.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ltc/compressed_stream.h"
#include "ltc/lzss_window.h"

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

std::string to_hex(const std::string& data)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (unsigned char c : data)
    {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0F]);
    }
    return out;
}

std::filesystem::path make_temp_dir()
{
    const auto dir = std::filesystem::temp_directory_path() / "ltc_m2_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

// сжимает буфер тем же путём, что и CLI
std::string compress(const std::filesystem::path& dir, const std::vector<std::uint8_t>& input)
{
    const std::string path = (dir / "out.ltc").string();
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

std::string unhex(const char* hex)
{
    const std::size_t len = std::strlen(hex);
    std::string       out;
    out.reserve(len / 2);
    for (std::size_t i = 0; i + 1 < len; i += 2)
    {
        const auto hi = static_cast<unsigned>(std::strchr("0123456789abcdef", hex[i]) - "0123456789abcdef");
        const auto lo = static_cast<unsigned>(std::strchr("0123456789abcdef", hex[i + 1]) - "0123456789abcdef");
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

//---------------------------------------------------------------------------
// Разбор потока: достаточно, чтобы проверить структуру (не декодер — он в M3)
//---------------------------------------------------------------------------
struct StreamInfo
{
    std::uint32_t version   = 0;
    std::uint64_t decoded   = 0;
    unsigned      literals  = 0;
    unsigned      spans     = 0;
    bool          ends_eos  = false;
    unsigned      pad_bits  = 0;   // биты, дочитанные нулями за концом файла
};

StreamInfo parse_stream(const std::string& data)
{
    StreamInfo  info;
    std::size_t byte_pos = 0;
    unsigned    mask     = 0;
    std::uint8_t curr    = 0;

    auto bit = [&]() -> unsigned {
        if (mask == 0)
        {
            if (byte_pos >= data.size())
            {
                ++info.pad_bits;
                return 0;
            }
            curr = static_cast<std::uint8_t>(data[byte_pos++]);
            mask = 1;
        }
        const unsigned value = (curr & mask) ? 1u : 0u;
        mask                 = (mask << 1) & 0xFF;
        return value;
    };

    auto field = [&](unsigned bits) -> std::uint32_t {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < bits; ++i)
        {
            value = (value << 1) | bit();
        }
        return value;
    };

    info.version = field(32);

    for (;;)
    {
        const unsigned flag = bit();
        if (flag == 1)
        {
            field(8);
            ++info.literals;
            ++info.decoded;
            info.ends_eos = false;
            continue;
        }

        const std::uint32_t offset = field(ltc::kNumOffsetBits);
        const std::uint32_t raw_len = field(ltc::kNumLengthBits);
        if (offset == 0)
        {
            info.ends_eos = true;
            break;
        }
        ++info.spans;
        info.decoded += raw_len + ltc::kBreakEvenPoint + 1;
        info.ends_eos = false;
    }

    return info;
}

//---------------------------------------------------------------------------
// Векторы, снятые с эталонного LTC (ref-build/build/ltc)
//---------------------------------------------------------------------------
struct Vector
{
    const char* m_Name;
    const char* m_Input;
    const char* m_LtcHex;
};

const Vector k_Vectors[] = {
    {"empty", "", "000000000000"},
    {"A", "A", "00000000050100"},
    {"AB", "AB", "00000000050b0100"},
    {"AAAA", "AAAA", "00000000050b162c0800"},
    {"17xA", "AAAAAAAAAAAAAAAAA", "00000000050b162c58b060c182050b162c58b060c182050100"},
    {"18xA", "AAAAAAAAAAAAAAAAAA", "00000000050b162c58b060c182050b162c58b060c182050b0200"},
    {"abcx7", "abcabcabcabcabcabcabc", "000000000d1b350680df1800"},
    {"A_30", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "000000000501e0172c58b060c182050b162c58b02000"},
};

void test_vectors()
{
    const auto dir = make_temp_dir();

    for (const Vector& vec : k_Vectors)
    {
        const std::string        input(vec.m_Input);
        const std::vector<std::uint8_t> bytes(input.begin(), input.end());
        const std::string        produced = compress(dir, bytes);
        const std::string        expected = unhex(vec.m_LtcHex);

        ++g_checks;
        if (produced != expected)
        {
            ++g_failures;
            std::cerr << "vector '" << vec.m_Name << "' mismatch\n  expected " << vec.m_LtcHex
                      << "\n  produced " << to_hex(produced) << "\n";
        }
    }

    std::filesystem::remove_all(dir);
}

void test_stream_invariants()
{
    const auto dir = make_temp_dir();

    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> cases;
    cases.emplace_back("empty", std::vector<std::uint8_t>{});
    cases.emplace_back("one", std::vector<std::uint8_t>{0x41});
    cases.emplace_back("run4", std::vector<std::uint8_t>{'A', 'A', 'A', 'A'});

    std::vector<std::uint8_t> ramp(256);
    for (int i = 0; i < 256; ++i)
    {
        ramp[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    }
    cases.emplace_back("ramp256", ramp);

    std::vector<std::uint8_t> run5000(5000, 'Z');
    cases.emplace_back("run5000", run5000);

    for (const auto& [name, bytes] : cases)
    {
        const std::string data = compress(dir, bytes);
        const StreamInfo  info = parse_stream(data);

        CHECK_EQ(info.version, 0u);
        CHECK(info.ends_eos);
        CHECK_EQ(info.decoded, bytes.size());
        CHECK(info.pad_bits < ltc::kLookAheadSize);
        // весь поток = 32 (версия) + 9*литералов + 17*span'ов + 17 (EOS);
        // в файл попадает floor(bits/8) байт, остаток (pad) теряется — Q2
        CHECK_EQ(data.size() * 8, 32u + static_cast<std::size_t>(info.literals) * 9 +
                                      static_cast<std::size_t>(info.spans) * 17 + 17 -
                                      info.pad_bits);
        (void)name;
    }

    // на длинных повторах литералов быть почти не должно
    {
        const std::string data = compress(dir, run5000);
        const StreamInfo  info = parse_stream(data);
        CHECK(info.spans > info.literals);
        CHECK(info.literals < 100u);
    }

    std::filesystem::remove_all(dir);
}

// Окно не должно «протекать» между запусками: два прохода дают тот же результат
void test_window_reinit()
{
    const auto dir = make_temp_dir();

    std::vector<std::uint8_t> first(9000, 'Q');
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        first[i] = static_cast<std::uint8_t>((i * 31) & 0xFF);
    }

    const std::string a = compress(dir, first);
    const std::string b = compress(dir, first);
    CHECK_EQ(a, b);

    // и после «грязного» прогона окно не влияет на следующий
    (void)compress(dir, std::vector<std::uint8_t>(4000, 0xFF));
    const std::string c = compress(dir, first);
    CHECK_EQ(a, c);

    std::filesystem::remove_all(dir);
}

} // namespace

int main()
{
    test_vectors();
    test_stream_invariants();
    test_window_reinit();

    std::cout << "checks: " << g_checks << ", failures: " << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
