#include "ltc/app.h"

#include <cstdint>
#include <cstring>
#include <iostream>

#include "ltc/command_line.h"
#include "ltc/compressed_stream.h"
#include "ltc/file.h"
#include "ltc/paths.h"

namespace ltc {
namespace {

// Дословная копия справки (599 байт, сверено с эталоном)
constexpr const char* kHelpText =
    "LTC - A tool for compression and decompression of LTA files\n"
    "Usage: LTC <filename> [-out <file>] [-view] [-verbose] [-help]\n"
    "\n"
    "<filename>         - The file to be changed. It must have the\n"
    "                     extension LTA or LTC.\n"
    "-out <file>        - Specifies the file to output the data (if\n"
    "                     none is specified, it will toggle the\n"
    "                     extension between LTA and LTC).\n"
    "-view              - Prints out the text version of the file\n"
    "                     to standard output.\n"
    "-verbose           - Enables printing out of information\n"
    "-help              - Displays help\n"
    "\n";

void print_help()
{
    std::cout << kHelpText;
}

// справка + сообщение, код 1 (как в оригинале)
int fail_with_help(const char* message)
{
    print_help();
    std::cout << message << '\n';
    return kExitError;
}

constexpr char kOptionDesignator = '-';

} // namespace

int run_args(const std::vector<std::string>& args)
{
    // аргументы склеиваются в одну строку — как Parser.Init(argc-1, argv+1, '-')
    std::vector<char*> raw;
    raw.reserve(args.size());
    for (const std::string& arg : args)
    {
        raw.push_back(const_cast<char*>(arg.c_str()));
    }

    CommandLineParser parser;
    parser.init(static_cast<std::uint32_t>(raw.size()), raw.data(), kOptionDesignator);

    parser.set_num_parameters("out", 1);
    parser.set_num_parameters("verbose", 0);
    parser.set_num_parameters("help", 0);
    parser.set_num_parameters("view", 0);

    // 1. справка запрашивается раньше всех проверок (даже раньше неизвестных опций)
    if (parser.is_option_set("help"))
    {
        print_help();
        return kExitOk;
    }

    // 2. неизвестные опции
    for (std::uint32_t option = 0; option < parser.num_options(); ++option)
    {
        const char* name = parser.option_name(option);
        if (::strcasecmp(name, "out") != 0 && ::strcasecmp(name, "verbose") != 0 &&
            ::strcasecmp(name, "help") != 0 && ::strcasecmp(name, "view") != 0)
        {
            print_help();
            std::cout << "Unknown option: " << name << '\n';
            return kExitError;
        }
    }

    // 3. обязательный файл
    if (parser.num_global_parameters() == 0)
    {
        return fail_with_help("A file to modify must be specified");
    }

    // 4. -out без значения
    if (parser.is_option_set("out") && parser.num_parameters("out") < 1)
    {
        return fail_with_help("A file name must be specified with -out");
    }

    // 5. -view и -out несовместимы
    if (parser.is_option_set("view") && parser.is_option_set("out"))
    {
        return fail_with_help("-view and -out cannot be set. Please select only one.");
    }

    // Q1: флаг принимается и нигде не используется — как в оригинале
    const bool verbose = parser.is_option_set("verbose");
    (void)verbose;

    const bool view = parser.is_option_set("view");

    const char* filename = parser.global_parameter(0);

    const bool input_compressed = is_file_compressed(filename);

    //-----------------------------------------------------------------------
    // вход
    RawReader        raw_input;
    CompressedReader compressed_input;
    bool             input_open = false;

    if (input_compressed)
    {
        if (compressed_input.open(filename) == CompressedOpen::kOpenFailed)
        {
            std::cout << "Error: Unable to open the file " << filename << '\n';
            return kExitError;
        }
        input_open = true;
    }
    else
    {
        input_open = raw_input.open(filename);
        if (!input_open)
        {
            std::cout << "Error: Unable to open the file " << filename << '\n';
            return kExitError;
        }
    }

    //-----------------------------------------------------------------------
    // просмотр
    if (view)
    {
        std::uint8_t value = 0;
        auto read = [&](std::uint8_t& out) {
            return input_compressed ? compressed_input.read_byte(out) : raw_input.read_byte(out);
        };
        while (read(value))
        {
            std::cout.put(static_cast<char>(value));
        }
        if (input_compressed)
        {
            compressed_input.close();
        }
        else
        {
            raw_input.close();
        }
        return kExitOk;
    }

    //-----------------------------------------------------------------------
    // имя выхода
    std::string out_name;

    if (parser.is_option_set("out"))
    {
        out_name = parser.parameter("out", 0);
    }
    else if (derive_output_name(filename, input_compressed, out_name) == NameResult::kBadExtension)
    {
        std::cout << "Error: The input file does not have a proper extension.\n";
        std::cout << "Either specify an output filename or correct the extension.\n";
        return kExitError;
    }

    //-----------------------------------------------------------------------
    // выход: сжатость — ОТРИЦАНИЕ сжатости входа (main.cpp: bInFileCompressed ? false : true)
    const bool output_compressed = !input_compressed;

    RawWriter        raw_output;
    CompressedWriter compressed_output;

    const bool output_open = output_compressed ? compressed_output.open(out_name.c_str())
                                               : raw_output.open(out_name.c_str());

    if (!output_open)
    {
        std::cout << "Error: Unable to open file " << out_name << " for writing.\n";
        return kExitError;
    }

    //-----------------------------------------------------------------------
    // построчная перекачка байт
    std::uint8_t value = 0;
    auto read = [&](std::uint8_t& out) {
        return input_compressed ? compressed_input.read_byte(out) : raw_input.read_byte(out);
    };
    auto write = [&](std::uint8_t in) {
        return output_compressed ? compressed_output.write_byte(in) : raw_output.write_byte(in);
    };

    while (read(value))
    {
        if (!write(value))
        {
            std::cout << "Error: Unable to write to " << out_name << '\n';
            std::cout << "Disk may be full.\n";
            return kExitError;
        }
    }

    if (output_compressed)
    {
        compressed_output.close();
    }
    else
    {
        raw_output.close();
    }

    if (input_compressed)
    {
        compressed_input.close();
    }
    else
    {
        raw_input.close();
    }

    return kExitOk;
}

int run(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    return run_args(args);
}

} // namespace ltc
