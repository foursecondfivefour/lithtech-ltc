#include "ltc/lzss_decoder.h"

namespace ltc {

LzssDecoder::LzssDecoder()
{
    // CLTACompressedFile::Open(READ): m_nDecSpanLen = 0; m_nDecWndPos = 1;
    // (окно в оригинале не трогается — см. Q6 в заголовке)
    for (std::uint8_t& byte : m_Window)
    {
        byte = 0;
    }
}

bool LzssDecoder::read_byte(std::uint8_t& value, BitReader& bits)
{
    // see if we are at the end of a span
    if (m_SpanLen == 0)
    {
        // see if it is raw data or a span
        bool type = false;
        if (bits.get_bit(type) == false)
        {
            // end of input stream
            return false;
        }

        if (type == false)
        {
            // we have a span: 12 бит offset, старшим вперёд
            m_SpanPos = bits.read_field(kNumOffsetBits);

            // see if this is the special end of stream token
            if (m_SpanPos == 0)
            {
                return false;
            }

            // now read in the span length
            m_SpanLen = bits.read_field(kNumLengthBits) + (kBreakEvenPoint + 1);
        }
        else
        {
            // we have a single character, read in the value (8 бит, старшим вперёд)
            value              = static_cast<std::uint8_t>(bits.read_field(8));
            m_Window[m_WndPos] = value;
            m_WndPos           = window_pos(m_WndPos + 1);
            return true;
        }
    }

    // так что у нас есть span
    value              = m_Window[m_SpanPos];
    m_Window[m_WndPos] = value;

    m_SpanPos = window_pos(m_SpanPos + 1);
    m_WndPos  = window_pos(m_WndPos + 1);
    --m_SpanLen;

    return true;
}

} // namespace ltc
