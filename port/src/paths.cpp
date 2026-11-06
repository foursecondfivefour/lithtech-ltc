#include "ltc/paths.h"

#include <cstring>
#include <strings.h>

namespace ltc {

bool is_file_compressed(const char* filename)
{
    const std::size_t len = std::strlen(filename);
    if (len == 0)
    {
        return false;
    }

    for (std::size_t pos = len; pos > 0; --pos)
    {
        if (filename[pos - 1] == '.')
        {
            // сравниваем остаток без точки — как в оригинале
            return ::strcasecmp(filename + pos, "ltc") == 0;
        }
    }

    return false;
}

const char* opposite_extension(bool input_compressed)
{
    // main.cpp: bInFileCompressed ? ".lta" : ".ltc"
    return input_compressed ? ".lta" : ".ltc";
}

NameResult derive_output_name(const char* input, bool input_compressed, std::string& out)
{
    const long len = static_cast<long>(std::strlen(input));

    long curr = len - 1;
    for (; curr >= 0; --curr)
    {
        if (input[curr] == '.')
        {
            break;
        }
    }

    if (curr < 0 || (::strcasecmp(input + curr, ".LTA") != 0 && ::strcasecmp(input + curr, ".LTC") != 0))
    {
        return NameResult::kBadExtension;
    }

    out.assign(input, static_cast<std::size_t>(curr));
    out += opposite_extension(input_compressed);
    return NameResult::kOk;
}

} // namespace ltc
