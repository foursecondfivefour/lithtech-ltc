// Порт CLZSSWindow (libs/ltamgr/lzsswindow.cpp) — построчно, с сохранением
// всех особенностей конструкции.
//
// Схема: LZSS с двоичным деревом поиска (Storer–Szymanski). Окно 4096 байт плюс
// «ghost edge» из LOOK_AHEAD_SIZE байт, чтобы сравнение 17 байт не проверяло границу
// массива. Дерево — m_Tree[4097], узел 4096 — корень, узел 0 — NULL (и он же EOS в формате).
//
// ВАЖНО (см. docs/porting-notes.md, Q8): позиция окна 0 никогда не попадает в дерево —
// AddNode() при nWindowNode == NULL_NODE выходит сразу, — поэтому offset 0 в потоке
// не может означать ничего, кроме EOS. Риск «настоящего» совпадения с offset 0,
// обсуждавшийся в ANALYSIS-4 §6.12, конструктивно исключён.
#pragma once

#include <cstdint>

#include "ltc/bits.h"

namespace ltc {

inline constexpr std::uint32_t kRootNode = kWindowSize;   // 4096
inline constexpr std::uint32_t kNullNode = 0;            // NULL_NODE == END_OF_STREAM

inline std::uint32_t window_pos(std::uint32_t v)
{
    return v & (kWindowSize - 1);
}

class LzssWindow
{
public:
    LzssWindow();

    // сбрасывает окно и дерево (в оригинале — конструктор + Init при открытии на запись)
    void init();

    // добавляет байт в look-ahead; при заполнении буфера выталкивает токен
    bool add_byte(std::uint8_t value, BitWriter& bits);

    // дописывает остаток и EOS; вызывается при закрытии
    bool flush_look_ahead(BitWriter& bits);

private:
    struct TreeNode
    {
        std::uint32_t m_Parent;
        std::uint32_t m_Smaller;
        std::uint32_t m_Larger;
    };

    bool is_look_ahead_full() const { return m_LookAheadLen >= kLookAheadSize; }

    bool write_token(BitWriter& bits);
    bool write_span(BitWriter& bits, std::uint32_t offset, std::uint32_t length);

    void collapse_node(std::uint32_t node, std::uint32_t child);
    void replace_node(std::uint32_t node, std::uint32_t replace_with);
    void delete_node(std::uint32_t node);
    void add_node(std::uint32_t window_node, std::uint32_t& match_pos, std::uint32_t& match_len);

    std::uint8_t m_Window[kWindowSize + kLookAheadSize];
    TreeNode     m_Tree[kWindowSize + 1];

    std::uint32_t m_LookAheadLen  = 0;
    std::uint32_t m_LookAheadPos  = 0;
    std::uint32_t m_MatchLen      = 0;
    std::uint32_t m_MatchPos      = 0;
    bool          m_LookAheadInit = false;
};

} // namespace ltc
