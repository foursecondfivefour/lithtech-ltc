// Порт декодера CLTACompressedFile (libs/ltamgr/ltacompressedfile.cpp, ReadByte).
//
// Схема ровно та же, что у компрессора: окно 4096, побайтовое копирование span'а
// (перекрытие разрешено), литералы пишутся в окно, позиция записи начинается с 1.
//
// Q6: оригинал окно НЕ обнуляет (m_DecWnd — член класса без инициализации), поэтому
// span, ссылающийся на ещё не записанную область, выдаёт содержимое стека. На корректных
// потоках этого не бывает (компрессор ссылается только назад). Порт обнуляет окно:
// это отклонение видно только на повреждённых файлах и зафиксировано в porting-notes.
#pragma once

#include <cstdint>

#include "ltc/bits.h"
#include "ltc/lzss_window.h"

namespace ltc {

class LzssDecoder
{
public:
    LzssDecoder();

    // false — конец потока (EOS или исчерпание битов)
    bool read_byte(std::uint8_t& value, BitReader& bits);

private:
    std::uint8_t  m_Window[kWindowSize];
    std::uint32_t m_WndPos  = 1;
    std::uint32_t m_SpanLen = 0;
    std::uint32_t m_SpanPos = 0;
};

} // namespace ltc
