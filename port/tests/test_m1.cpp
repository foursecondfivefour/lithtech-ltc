// Юнит-тесты M1: парсер командной строки, определение сжатости, имя выхода,
// битовый писатель и сообщения CLI. Без внешних зависимостей — чтобы гейт не
// требовал сети.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ltc/app.h"
#include "ltc/bits.h"
#include "ltc/command_line.h"
#include "ltc/compressed_stream.h"
#include "ltc/paths.h"

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
                      << __FILE__ << ":" << __LINE__ << ")\n  left  = " << va        \
                      << "\n  right = " << vb << "\n";                                \
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
    char buf[4096];
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
    const auto dir = std::filesystem::temp_directory_path() / "ltc_m1_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

//-----------------------------------------------------------------------------
// Парсер командной строки
//-----------------------------------------------------------------------------

void test_parser()
{
    {
        // опция с параметром + глобальный параметр
        std::vector<std::string> args = {"x.lta", "-out", "y.ltc"};
        std::vector<char*>       raw;
        for (auto& a : args)
        {
            raw.push_back(const_cast<char*>(a.c_str()));
        }
        ltc::CommandLineParser parser;
        CHECK(parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-'));
        CHECK_EQ(parser.num_options(), 1u);
        CHECK(parser.is_option_set("out"));
        CHECK(!parser.is_option_set("view"));
        CHECK_EQ(std::strcmp(parser.parameter("out", 0), "y.ltc"), 0);
        CHECK_EQ(parser.num_global_parameters(), 1u);
        CHECK_EQ(std::strcmp(parser.global_parameter(0), "x.lta"), 0);
        CHECK(parser.global_parameter(1) == nullptr);
    }

    {
        // регистр имени опции не важен
        std::vector<std::string> args = {"x.lta", "-OUT", "y.ltc"};
        std::vector<char*>       raw;
        for (auto& a : args)
        {
            raw.push_back(const_cast<char*>(a.c_str()));
        }
        ltc::CommandLineParser parser;
        parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-');
        CHECK(parser.is_option_set("out"));
    }

    {
        // лишние параметры после -out уезжают в глобальные, в конец списка
        std::vector<std::string> args = {"-out", "a", "b", "c"};
        std::vector<char*>       raw;
        for (auto& a : args)
        {
            raw.push_back(const_cast<char*>(a.c_str()));
        }
        ltc::CommandLineParser parser;
        parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-');
        CHECK_EQ(parser.num_parameters("out"), 3u);
        CHECK_EQ(parser.set_num_parameters("out", 1), 2u);
        CHECK_EQ(parser.num_parameters("out"), 1u);
        CHECK_EQ(parser.num_global_parameters(), 2u);
        CHECK_EQ(std::strcmp(parser.global_parameter(0), "b"), 0);
        CHECK_EQ(std::strcmp(parser.global_parameter(1), "c"), 0);
    }

    {
        // слово с пробелом разваливается на два (склейка через пробел + split)
        std::vector<std::string> args = {"my file.lta", "-view"};
        std::vector<char*>       raw;
        for (auto& a : args)
        {
            raw.push_back(const_cast<char*>(a.c_str()));
        }
        ltc::CommandLineParser parser;
        parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-');
        CHECK_EQ(parser.num_global_parameters(), 2u);
        CHECK_EQ(std::strcmp(parser.global_parameter(0), "my"), 0);
        CHECK_EQ(std::strcmp(parser.global_parameter(1), "file.lta"), 0);
    }

    {
        // один дезигнатор — опция с пустым именем
        std::vector<std::string> args = {"-"};
        std::vector<char*>       raw;
        for (auto& a : args)
        {
            raw.push_back(const_cast<char*>(a.c_str()));
        }
        ltc::CommandLineParser parser;
        parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-');
        CHECK_EQ(parser.num_options(), 1u);
        CHECK_EQ(std::strcmp(parser.option_name(0), ""), 0);
        CHECK(!parser.is_option_set("out"));
        // квирк: имя опции пустое, и is_option_set("") для неё ИСТИННО
        // (strcasecmp с пустой строкой), поэтому main выводит "Unknown option: ".
        // Эталон ведёт себя так же — проверяется в scripts/cli_parity.sh.
        CHECK(parser.is_option_set(""));
    }

    {
        // предел количества опций: 65-я отвергается, первые 64 остаются
        std::vector<std::string> args;
        std::vector<char*>       raw;
        args.reserve(65);
        for (int i = 0; i < 65; ++i)
        {
            args.push_back("-o" + std::to_string(i));
        }
        for (auto& a : args)
        {
            raw.push_back(const_cast<char*>(a.c_str()));
        }
        ltc::CommandLineParser parser;
        CHECK(!parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), '-'));
        CHECK_EQ(parser.num_options(), ltc::CommandLineParser::kMaxOptions);
    }
}

//-----------------------------------------------------------------------------
// Определение сжатости
//-----------------------------------------------------------------------------

void test_is_file_compressed()
{
    CHECK(!ltc::is_file_compressed(""));
    CHECK(!ltc::is_file_compressed("ltc"));
    CHECK(!ltc::is_file_compressed("a"));
    CHECK(!ltc::is_file_compressed("a.lta"));
    CHECK(ltc::is_file_compressed("a.ltc"));
    CHECK(ltc::is_file_compressed("a.LTC"));
    CHECK(ltc::is_file_compressed("a.LtC"));
    CHECK(ltc::is_file_compressed(".ltc"));          // одни только расширения
    CHECK(ltc::is_file_compressed("a.b.ltc"));       // точка в имени
    CHECK(!ltc::is_file_compressed("a.ltc.bak"));    // смотрим последнюю точку
    CHECK(!ltc::is_file_compressed("dir.ltc/x.lta"));
    CHECK(ltc::is_file_compressed("/tmp/dir/x.LTC"));
}

//-----------------------------------------------------------------------------
// Имя выхода
//-----------------------------------------------------------------------------

void test_derive_output_name()
{
    std::string out;

    CHECK(ltc::derive_output_name("x.lta", false, out) == ltc::NameResult::kOk);
    CHECK_EQ(out, std::string("x.ltc"));

    CHECK(ltc::derive_output_name("x.ltc", true, out) == ltc::NameResult::kOk);
    CHECK_EQ(out, std::string("x.lta"));

    // регистр расширения теряется — как в оригинале
    CHECK(ltc::derive_output_name("x.LTA", false, out) == ltc::NameResult::kOk);
    CHECK_EQ(out, std::string("x.ltc"));

    CHECK(ltc::derive_output_name("a.b.LTC", true, out) == ltc::NameResult::kOk);
    CHECK_EQ(out, std::string("a.b.lta"));

    CHECK(ltc::derive_output_name("noext", false, out) == ltc::NameResult::kBadExtension);
    CHECK(ltc::derive_output_name("dir.d/file", false, out) == ltc::NameResult::kBadExtension);
    CHECK(ltc::derive_output_name(".hidden", false, out) == ltc::NameResult::kBadExtension);
    CHECK(ltc::derive_output_name("x.lta.bak", false, out) == ltc::NameResult::kBadExtension);

    // Q5: длинные пути не обрезаются (в оригинале здесь UB)
    const std::string long_name(300, 'd');
    CHECK(ltc::derive_output_name((long_name + ".lta").c_str(), false, out) == ltc::NameResult::kOk);
    CHECK_EQ(out.size(), long_name.size() + 4);
}

//-----------------------------------------------------------------------------
// Битовый писатель и поток .ltc
//-----------------------------------------------------------------------------

// независимая упаковка: версия + литералы + EOS, хвост отбрасывается (Q2)
std::string expected_stream(const std::vector<std::uint8_t>& bytes)
{
    std::vector<int> bits;
    auto push_field = [&bits](std::uint32_t value, unsigned n) {
        for (unsigned i = 1; i <= n; ++i)
        {
            bits.push_back(static_cast<int>((value >> (n - i)) & 1u));
        }
    };

    push_field(0, 32);
    for (std::uint8_t b : bytes)
    {
        bits.push_back(1);
        push_field(b, 8);
    }
    bits.push_back(0);
    push_field(0, 12);
    push_field(0, 4);

    std::string out;
    std::uint8_t curr = 0;
    unsigned count = 0;
    for (int bit : bits)
    {
        if (bit)
        {
            curr = static_cast<std::uint8_t>(curr | (1u << count));
        }
        if (++count == 8)
        {
            out.push_back(static_cast<char>(curr));
            curr  = 0;
            count = 0;
        }
    }
    return out;   // недописанный байт теряется
}

void test_bit_writer()
{
    const auto dir = make_temp_dir();

    {
        // пустой файл -> 6 байт (32 версии + 17 EOS = 49 бит)
        const std::string path = (dir / "empty.ltc").string();
        ltc::CompressedWriter writer;
        CHECK(writer.open(path.c_str()));
        CHECK(writer.close());

        const std::string data = read_file(path);
        CHECK_EQ(data.size(), 6u);
        CHECK_EQ(data, std::string(6, '\0'));
        CHECK_EQ(data, expected_stream({}));
    }

    {
        // 1 байт -> 7 байт, и побайтовое совпадение с независимой упаковкой
        const std::string path = (dir / "one.ltc").string();
        ltc::CompressedWriter writer;
        CHECK(writer.open(path.c_str()));
        CHECK(writer.write_byte(0x41));
        CHECK(writer.close());

        const std::string data = read_file(path);
        CHECK_EQ(data.size(), 7u);
        CHECK_EQ(data, expected_stream({0x41}));
    }

    {
        // 256 значений — порядок бит не должен зависеть от значения
        std::vector<std::uint8_t> all;
        for (int i = 0; i < 256; ++i)
        {
            all.push_back(static_cast<std::uint8_t>(i));
        }
        const std::string path = (dir / "all.ltc").string();
        ltc::CompressedWriter writer;
        CHECK(writer.open(path.c_str()));
        for (std::uint8_t b : all)
        {
            CHECK(writer.write_byte(b));
        }
        CHECK(writer.close());

        CHECK_EQ(read_file(path), expected_stream(all));
    }

    {
        // BitWriter: поля пишутся старшим битом вперёд, биты в байте — от младшего
        std::FILE* f = std::fopen((dir / "bits.bin").string().c_str(), "wb");
        CHECK(f != nullptr);
        {
            ltc::BitWriter bits(f);
            bits.write_field(0xA5, 8);   // 1010 0101
            bits.close();
        }
        std::fclose(f);
        // 1010 0101 старшим вперёд -> в байте биты 7..0 = 1,0,1,0,0,1,0,1
        // упаковка от младшего: b0=1, b1=0, b2=1, b3=0, b4=0, b5=1, b6=0, b7=1 = 0xA5
        CHECK_EQ(read_file((dir / "bits.bin").string()), std::string(1, static_cast<char>(0xA5)));
    }

    std::filesystem::remove_all(dir);
}

//-----------------------------------------------------------------------------
// Сообщения CLI (без эталона: тексты сверяются с docs/ANALYSIS-4.md §3)
//-----------------------------------------------------------------------------

std::string capture_stdout(const std::vector<std::string>& args, int& code)
{
    std::ostringstream buffer;
    std::streambuf*    old = std::cout.rdbuf(buffer.rdbuf());
    code = ltc::run_args(args);
    std::cout.rdbuf(old);
    return buffer.str();
}

std::string capture_all(const std::vector<std::string>& args, int& code)
{
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf*    old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf*    old_err = std::cerr.rdbuf(err.rdbuf());
    code = ltc::run_args(args);
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    return out.str() + err.str();
}

void test_cli_messages()
{
    int code = 0;

    const std::string help = capture_stdout({"-help"}, code);
    CHECK_EQ(code, 0);
    CHECK_EQ(help.size(), 599u);
    CHECK_EQ(help.rfind("LTC - A tool for compression and decompression of LTA files\n", 0), 0u);
    CHECK(help.find("-verbose           - Enables printing out of information\n") != std::string::npos);

    const std::string none = capture_stdout({}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(none, help + "A file to modify must be specified\n");

    const std::string unknown = capture_stdout({"-bogus"}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(unknown, help + "Unknown option: bogus\n");

    // справка перекрывает неизвестную опцию
    const std::string help_wins = capture_stdout({"-help", "-bogus"}, code);
    CHECK_EQ(code, 0);
    CHECK_EQ(help_wins, help);

    // порядок: сначала «нужен файл», потом «-out без значения»
    const std::string out_alone = capture_stdout({"-out"}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(out_alone, help + "A file to modify must be specified\n");

    const std::string out_no_value = capture_stdout({"x.lta", "-out"}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(out_no_value, help + "A file name must be specified with -out\n");

    const std::string view_out = capture_stdout({"x.lta", "-view", "-out", "y.ltc"}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(view_out, help + "-view and -out cannot be set. Please select only one.\n");

    // несуществующий файл
    const std::string missing = capture_stdout({"definitely-missing.lta"}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(missing, "Error: Unable to open the file definitely-missing.lta\n");

    // пустой .ltc открывается (версия 0 из дочитанных нулей) и декодируется в пустоту
    const auto dir = make_temp_dir();
    const auto ltc_path = (dir / "x.ltc").string();
    std::FILE* f = std::fopen(ltc_path.c_str(), "wb");
    CHECK(f != nullptr);
    for (int i = 0; i < 4; ++i)
    {
        std::fputc(0, f);
    }
    std::fclose(f);
    const std::string empty_ok = capture_stdout({ltc_path, "-out", (dir / "x.lta").string()}, code);
    CHECK_EQ(code, 0);
    CHECK_EQ(empty_ok, std::string());

    // версия != 0 -> «Unable to open the file», как у эталона
    const auto bad_path = (dir / "bad.ltc").string();
    f = std::fopen(bad_path.c_str(), "wb");
    CHECK(f != nullptr);
    std::fputc(1, f);
    std::fputc(0, f);
    std::fputc(0, f);
    std::fputc(0, f);
    std::fclose(f);
    const std::string bad_version = capture_stdout({bad_path}, code);
    CHECK_EQ(code, 1);
    CHECK_EQ(bad_version, "Error: Unable to open the file " + bad_path + "\n");

    std::filesystem::remove_all(dir);
}

} // namespace

int main()
{
    test_parser();
    test_is_file_compressed();
    test_derive_output_name();
    test_bit_writer();
    test_cli_messages();

    std::cout << "checks: " << g_checks << ", failures: " << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
