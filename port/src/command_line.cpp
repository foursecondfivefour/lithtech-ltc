#include "ltc/command_line.h"

#include <cctype>
#include <cstring>
#include <strings.h>

namespace ltc {

void CommandLineParser::free()
{
    m_Line.clear();
    m_NumOptions = 0;
    // ВНИМАНИЕ: как и в оригинале, m_Globals здесь не сбрасывается (Free() сбрасывает
    // только строку и счётчик опций). На практике Init вызывается один раз на объект,
    // поэтому поведение идентично; отличие проявилось бы только при повторном Init.
}

bool CommandLineParser::init(std::uint32_t arg_count, char** arg_strings, char designator)
{
    free();

    // оригинал: new char[sum(len)+count+1], затем strcat(arg), strcat(" ")
    m_Line.clear();
    for (std::uint32_t i = 0; i < arg_count; ++i)
    {
        m_Line += arg_strings[i];
        m_Line += ' ';
    }

    return internal_init(designator);
}

bool CommandLineParser::internal_init(char designator)
{
    // ASSERT(!isspace(designator)) — в оригинале проверка не работает в release

    const std::uint32_t str_len = static_cast<std::uint32_t>(m_Line.size());

    // пробелы -> '\0', чтобы возвращать подстроки без копирования
    for (std::uint32_t i = 0; i < str_len; ++i)
    {
        if (std::isspace(static_cast<unsigned char>(m_Line[i])))
        {
            m_Line[i] = '\0';
        }
    }

    const char* line = m_Line.c_str();

    for (std::uint32_t i = 0; i < str_len; ++i)
    {
        const bool word_start = (i == 0) || (line[i - 1] == '\0');

        if ((line[i] == designator) && word_start)
        {
            if (m_NumOptions >= kMaxOptions)
            {
                return false;
            }

            // +1 — пропускаем сам дезигнатор
            m_Options[m_NumOptions].m_Pos           = i + 1;
            m_Options[m_NumOptions].m_NumParameters = 0;
            ++m_NumOptions;
        }
        else if ((line[i] != '\0') && word_start)
        {
            Option* option = (m_NumOptions > 0) ? &m_Options[m_NumOptions - 1] : &m_Globals;

            if (option->m_NumParameters >= kMaxParameters)
            {
                return false;
            }

            option->m_ParamOffset[option->m_NumParameters] = i;
            ++option->m_NumParameters;
        }
    }

    return true;
}

const CommandLineParser::Option* CommandLineParser::find_option(const char* name) const
{
    const char* line = m_Line.c_str();

    for (std::uint32_t i = 0; i < m_NumOptions; ++i)
    {
        if (::strcasecmp(line + m_Options[i].m_Pos, name) == 0)
        {
            return &m_Options[i];
        }
    }

    return nullptr;
}

bool CommandLineParser::is_option_set(const char* option) const
{
    return find_option(option) != nullptr;
}

std::uint32_t CommandLineParser::num_parameters(const char* option) const
{
    const Option* found = find_option(option);
    return found ? found->m_NumParameters : 0u;
}

std::uint32_t CommandLineParser::set_num_parameters(const char* option, std::uint32_t count)
{
    m_LastMoved = 0;

    Option* found = const_cast<Option*>(find_option(option));
    if (found == nullptr)
    {
        return 0;
    }

    if (found->m_NumParameters <= count)
    {
        return 0;
    }

    for (std::uint32_t i = count; i < found->m_NumParameters; ++i)
    {
        if (m_Globals.m_NumParameters >= kMaxParameters)
        {
            break;
        }

        // лишние параметры дописываются в КОНЕЦ списка глобальных (как в оригинале)
        m_Globals.m_ParamOffset[m_Globals.m_NumParameters] = found->m_ParamOffset[i];
        ++m_Globals.m_NumParameters;
        ++m_LastMoved;
    }

    found->m_NumParameters = count;
    return m_LastMoved;
}

const char* CommandLineParser::parameter(const char* option, std::uint32_t index) const
{
    const Option* found = find_option(option);
    if (found && index < found->m_NumParameters)
    {
        return m_Line.c_str() + found->m_ParamOffset[index];
    }
    return nullptr;
}

const char* CommandLineParser::global_parameter(std::uint32_t index) const
{
    if (index < m_Globals.m_NumParameters)
    {
        return m_Line.c_str() + m_Globals.m_ParamOffset[index];
    }
    return nullptr;
}

const char* CommandLineParser::option_name(std::uint32_t index) const
{
    if (index < m_NumOptions)
    {
        return m_Line.c_str() + m_Options[index].m_Pos;
    }
    return nullptr;
}

} // namespace ltc
