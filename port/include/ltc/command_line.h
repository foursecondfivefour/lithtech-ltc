// Порт CCommandLineParser (tools/LTC/commandlineparser.cpp) — 1:1 по семантике.
//
// Все аргументы склеиваются в одну строку через пробел, затем каждый пробельный
// символ заменяется на '\0'. Слово, начинающееся с дезигнатора ('-'), открывает
// новую опцию; остальные слова становятся параметрами последней опции, а если
// опций ещё не было — глобальными параметрами. Отсюда неочевидное следствие,
// которое порт обязан сохранить: имя файла с пробелом разваливается на два слова.
#pragma once

#include <cstdint>
#include <string>

namespace ltc {

class CommandLineParser
{
public:
    enum : std::uint32_t
    {
        kMaxOptions    = 64,
        kMaxParameters = 128
    };

    // dos-style: массив строк (argc-1, argv+1)
    bool init(std::uint32_t arg_count, char** arg_strings, char designator);

    bool          is_option_set(const char* option) const;
    std::uint32_t num_parameters(const char* option) const;
    std::uint32_t set_num_parameters(const char* option, std::uint32_t count);
    std::uint32_t num_options() const { return m_NumOptions; }
    const char*   parameter(const char* option, std::uint32_t index) const;
    std::uint32_t num_global_parameters() const { return m_Globals.m_NumParameters; }
    const char*   global_parameter(std::uint32_t index) const;
    const char*   option_name(std::uint32_t index) const;

    // сколько параметров было перенесено в глобальные вызовом set_num_parameters
    std::uint32_t last_moved_count() const { return m_LastMoved; }

private:
    struct Option
    {
        std::uint32_t m_Pos           = 0;
        std::uint32_t m_NumParameters = 0;
        std::uint32_t m_ParamOffset[kMaxParameters] = {};
    };

    void          free();
    bool          internal_init(char designator);
    const Option* find_option(const char* name) const;

    std::string   m_Line;
    std::uint32_t m_NumOptions = 0;
    Option        m_Options[kMaxOptions];
    Option        m_Globals;
    std::uint32_t m_LastMoved  = 0;
};

} // namespace ltc
