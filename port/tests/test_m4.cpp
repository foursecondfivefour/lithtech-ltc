// Юнит-тесты M4: инварианты формата и свойств, которые не зависят от эталона.
//
// Здесь проверяется то, что должно быть верно для ЛЮБОГО корректного потока:
// границы span'ов, диапазон offset, единственный EOS в конце, маскирование окна,
// обратимость битового слоя и round-trip на структурно разнообразных данных.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "ltc/app.h"
#include "ltc/bits.h"
#include "ltc/command_line.h"
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

std::filesystem::path make_temp_dir()
{
    const auto dir = std::filesystem::temp_directory_path() / "ltc_m4_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

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

std::string decompress(const std::filesystem::path& dir, const std::string& stream)
{
    const std::string path = (dir / "d.ltc").string();
    if (std::FILE* f = std::fopen(path.c_str(), "wb"))
    {
        std::fwrite(stream.data(), 1, stream.size(), f);
        std::fclose(f);
    }

    ltc::CompressedReader reader;
    if (reader.open(path.c_str()) != ltc::CompressedOpen::kOk)
    {
        return "<open failed>";
    }
    std::string  out;
    std::uint8_t value = 0;
    while (reader.read_byte(value))
    {
        out.push_back(static_cast<char>(value));
    }
    reader.close();
    return out;
}

//---------------------------------------------------------------------------
// Разбор потока: инварианты структуры
//---------------------------------------------------------------------------
struct StreamInfo
{
    unsigned      literals  = 0;
    unsigned      spans     = 0;
    unsigned      eos_count = 0;
    bool          eos_last  = false;
    std::uint64_t decoded   = 0;
    unsigned      pad_bits  = 0;
    bool          span_bounds_ok = true;
    bool          first_eos_seen = false;
};

StreamInfo analyze(const std::string& data)
{
    StreamInfo  info;
    std::size_t pos  = 0;
    unsigned    mask = 0;
    std::uint8_t curr = 0;

    auto bit = [&]() -> unsigned {
        if (mask == 0)
        {
            if (pos >= data.size())
            {
                ++info.pad_bits;
                return 0;
            }
            curr = static_cast<std::uint8_t>(data[pos++]);
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

    field(32);   // версия

    for (;;)
    {
        const unsigned flag = bit();
        if (flag == 1)
        {
            field(8);
            ++info.literals;
            ++info.decoded;
            info.eos_last = false;
            continue;
        }

        const std::uint32_t offset   = field(ltc::kNumOffsetBits);
        const std::uint32_t raw_len  = field(ltc::kNumLengthBits);
        if (offset == 0)
        {
            ++info.eos_count;
            info.eos_last = true;
            break;
        }

        const std::uint32_t length = raw_len + ltc::kBreakEvenPoint + 1;
        ++info.spans;
        info.decoded += length;
        info.eos_last = false;

        // инварианты формата: длина 2..17, offset внутри окна
        if (length < 2 || length > ltc::kLookAheadSize || offset >= ltc::kWindowSize)
        {
            info.span_bounds_ok = false;
        }
    }

    return info;
}

//---------------------------------------------------------------------------
// 1. Константы формата и маскирование окна
//---------------------------------------------------------------------------
void test_constants()
{
    CHECK_EQ(ltc::kNumOffsetBits, 12u);
    CHECK_EQ(ltc::kNumLengthBits, 4u);
    CHECK_EQ(ltc::kWindowSize, 4096u);
    CHECK_EQ(ltc::kBreakEvenPoint, 1u);
    CHECK_EQ(ltc::kLookAheadSize, 17u);
    CHECK_EQ(ltc::kRootNode, 4096u);
    CHECK_EQ(ltc::kNullNode, 0u);
    CHECK_EQ(ltc::kCurrFileVersion, 0u);

    // span длиной от kBreakEvenPoint+1 до (1<<kNumLengthBits)+kBreakEvenPoint+1
    CHECK_EQ(ltc::kBreakEvenPoint + 1, 2u);
    // максимальная длина span'а = максимальный 4-битный код + BREAK_EVEN_POINT + 1
    CHECK_EQ(((1u << ltc::kNumLengthBits) - 1) + ltc::kBreakEvenPoint + 1, ltc::kLookAheadSize);

    // WINDOW_POS(a) = a & (WINDOW_SIZE - 1)
    CHECK_EQ(ltc::window_pos(0), 0u);
    CHECK_EQ(ltc::window_pos(4095), 4095u);
    CHECK_EQ(ltc::window_pos(4096), 0u);
    CHECK_EQ(ltc::window_pos(4097), 1u);
    CHECK_EQ(ltc::window_pos(12288), 0u);
    CHECK_EQ(ltc::window_pos(4096 + 17), 17u);

    // в окне ровно 4096 «настоящих» позиций, поэтому offset 4096 невозможен
    CHECK_EQ(ltc::kWindowSize, ltc::kRootNode);
}

//---------------------------------------------------------------------------
// 2. Битовый слой: поле -> байты -> поле
//---------------------------------------------------------------------------
void test_bit_layer_roundtrip()
{
    const auto dir = make_temp_dir();

    std::mt19937                    rng(12345);
    std::vector<std::pair<std::uint32_t, unsigned>> fields;
    for (int i = 0; i < 200; ++i)
    {
        const unsigned        bits  = 1 + (rng() % 32);
        const std::uint32_t   value = static_cast<std::uint32_t>(rng()) &
                                     ((bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u));
        fields.emplace_back(value, bits);
    }

    const std::string path = (dir / "bits.bin").string();
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        CHECK(f != nullptr);
        ltc::BitWriter writer(f);
        for (const auto& [value, bits] : fields)
        {
            writer.write_field(value, bits);
        }
        writer.close();
        std::fclose(f);
    }

    ltc::RawReader reader;
    CHECK(reader.open(path.c_str()));
    ltc::BitReader bits(reader);
    for (const auto& [value, count] : fields)
    {
        CHECK_EQ(bits.read_field(count), value);
    }

    std::filesystem::remove_all(dir);
}

//---------------------------------------------------------------------------
// 3. Инварианты потока и round-trip на разнородных данных
//---------------------------------------------------------------------------
void test_stream_invariants_and_roundtrip()
{
    const auto dir = make_temp_dir();

    std::mt19937 rng(777);
    int          cases = 0;

    for (int mode = 0; mode < 6; ++mode)
    {
        for (int size : {0, 1, 2, 3, 16, 17, 18, 19, 100, 1000, 4095, 4096, 4097, 9000})
        {
            std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
            for (int i = 0; i < size; ++i)
            {
                const std::uint8_t random_byte = static_cast<std::uint8_t>(rng() & 0xFF);
                switch (mode)
                {
                    case 0: data[static_cast<std::size_t>(i)] = random_byte; break;
                    case 1: data[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i); break;
                    case 2: data[static_cast<std::size_t>(i)] = 'Z'; break;
                    case 3:
                        data[static_cast<std::size_t>(i)] =
                            static_cast<std::uint8_t>((i % 17) * 3); break;
                    case 4: data[static_cast<std::size_t>(i)] = (i % 3 == 0) ? 'a' : 'b'; break;
                    default:
                        data[static_cast<std::size_t>(i)] =
                            static_cast<std::uint8_t>((i / 64) & 0xFF);
                        break;
                }
            }

            const std::string packed = compress(dir, data);
            CHECK(packed.size() >= 6);

            const StreamInfo info = analyze(packed);
            ++cases;

            CHECK_EQ(info.eos_count, 1u);
            CHECK(info.eos_last);
            CHECK(info.span_bounds_ok);
            CHECK_EQ(info.decoded, data.size());
            CHECK(info.pad_bits < ltc::kLookAheadSize);
            CHECK_EQ(decompress(dir, packed).size(), data.size());
            CHECK_EQ(decompress(dir, packed), std::string(data.begin(), data.end()));
        }
    }

    CHECK_EQ(cases, 84);
    std::filesystem::remove_all(dir);
}

//---------------------------------------------------------------------------
// 4. Пределы парсера командной строки
//---------------------------------------------------------------------------
void test_parser_limits()
{
    // сначала собираем строки целиком, потом берём указатели: иначе push_back
    // переаллоцирует вектор и ранее взятые c_str() повиснут
    std::vector<std::string> args;
    args.reserve(201);
    args.push_back("file.lta");
    for (int i = 0; i < 200; ++i)
    {
        args.push_back("p" + std::to_string(i));
    }
    std::vector<char*> raw;
    raw.reserve(args.size());
    for (const std::string& arg : args)
    {
        raw.push_back(const_cast<char*>(arg.c_str()));
    }

    ltc::CommandLineParser parser;
    CHECK(!parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-'));
    CHECK_EQ(parser.num_global_parameters(), ltc::CommandLineParser::kMaxParameters);
    CHECK_EQ(std::strcmp(parser.global_parameter(0), "file.lta"), 0);
}

//---------------------------------------------------------------------------
// 5. Структурное свойство: сжатие не раздувает сильнее, чем 9 бит на байт
//---------------------------------------------------------------------------
void test_expansion_bound()
{
    const auto dir = make_temp_dir();

    std::mt19937              rng(2024);
    std::vector<std::uint8_t> data(20000);
    for (std::uint8_t& byte : data)
    {
        byte = static_cast<std::uint8_t>(rng() & 0xFF);
    }

    const std::string packed = compress(dir, data);

    // худший случай — все токены литеральные: 32 бита заголовка + 9 бит на байт + 17 бит EOS,
    // упакованных в байты с отбрасыванием хвоста
    const std::uint64_t worst_bits = 32 + 9ull * data.size() + 17;
    CHECK(packed.size() <= (worst_bits / 8) + 1);
    CHECK(packed.size() > data.size());   // случайные данные действительно раздуваются

    std::filesystem::remove_all(dir);
}

} // namespace

int main()
{
    test_constants();
    test_bit_layer_roundtrip();
    test_stream_invariants_and_roundtrip();
    test_parser_limits();
    test_expansion_bound();

    std::cout << "checks: " << g_checks << ", failures: " << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
