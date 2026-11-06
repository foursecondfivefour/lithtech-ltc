#include "ltc/lzss_window.h"

namespace ltc {

LzssWindow::LzssWindow()
{
    init();
}

void LzssWindow::init()
{
    // reset the writing position
    m_LookAheadPos = 1;
    m_LookAheadLen = 0;

    // clear out all of the nodes
    for (std::uint32_t node = 0; node < kWindowSize + 1; ++node)
    {
        m_Tree[node].m_Parent  = kNullNode;
        m_Tree[node].m_Larger  = kNullNode;
        m_Tree[node].m_Smaller = kNullNode;
    }

    // reset the tree
    m_Tree[kRootNode].m_Parent  = kRootNode;
    m_Tree[kRootNode].m_Larger  = m_LookAheadPos;
    m_Tree[kRootNode].m_Smaller = kNullNode;

    // set up the first node
    m_Tree[m_LookAheadPos].m_Parent  = kRootNode;
    m_Tree[m_LookAheadPos].m_Smaller = kNullNode;
    m_Tree[m_LookAheadPos].m_Larger  = kNullNode;

    m_MatchLen = 0;
    m_MatchPos = 0;

    m_LookAheadInit = false;

    // Оригинал окно не обнуляет. Читаются всегда только записанные байты (см. разбор
    // в docs/ANALYSIS-4.md §5), поэтому обнуление не влияет на вывод — но делает
    // поведение детерминированным под санитайзерами. Побайтовый паритет это
    // подтверждает (scripts/m2_gate.sh).
    for (std::uint8_t& byte : m_Window)
    {
        byte = 0;
    }
}

bool LzssWindow::add_byte(std::uint8_t value, BitWriter& bits)
{
    // make sure that the look ahead buffer is initialized
    if (m_LookAheadInit == false)
    {
        m_Window[m_LookAheadPos + m_LookAheadLen] = value;
        ++m_LookAheadLen;

        if (m_LookAheadLen >= kLookAheadSize)
        {
            m_LookAheadInit = true;
        }
        return true;
    }

    // see if we have a full buffer
    if (is_look_ahead_full())
    {
        // the buffer is full, blast out a token
        if (write_token(bits) == false)
        {
            // disk must be full
            return false;
        }
    }

    // now we need to fill in the look ahead buffer

    // remove the node we are just about to overwrite
    delete_node(window_pos(m_LookAheadPos + kLookAheadSize));

    // put the new value into the window

    // see if we are near the edge, if so, we need to write the ghost buffer
    if (m_LookAheadPos >= (kWindowSize - kLookAheadSize))
    {
        m_Window[window_pos(m_LookAheadPos + kLookAheadSize)] = value;

        // write to the ghost buffer
        m_Window[m_LookAheadPos + kLookAheadSize] = value;

        // advance our cursor through the window
        m_LookAheadPos = window_pos(m_LookAheadPos + 1);
    }
    else
    {
        // aren't near the edge, we can do all the unprotected accesses
        m_Window[m_LookAheadPos + kLookAheadSize] = value;
        ++m_LookAheadPos;
    }

    // add this new position into the tree
    add_node(m_LookAheadPos, m_MatchPos, m_MatchLen);

    // adjust our size
    ++m_LookAheadLen;

    return true;
}

bool LzssWindow::flush_look_ahead(BitWriter& bits)
{
    // if they have already filled the look ahead buffer, but the look ahead buffer
    // is not full now, that means we need to skip along in the window
    if (m_LookAheadInit)
    {
        m_LookAheadPos = window_pos(m_LookAheadPos + (kLookAheadSize - m_LookAheadLen));
    }

    // just write out tokens until there is nothing in the input buffer anymore
    while (m_LookAheadLen > 0)
    {
        m_MatchLen = 0;

        if (write_token(bits) == false)
        {
            return false;
        }

        // advance our cursor through the window
        m_LookAheadPos = window_pos(m_LookAheadPos + 1);
    }

    // output the end of stream character
    write_span(bits, 0, kBreakEvenPoint + 1);

    return true;
}

bool LzssWindow::write_span(BitWriter& bits, std::uint32_t offset, std::uint32_t length)
{
    // set a bit that indicates that it is a run
    bits.set_bit(0);

    // write out the offset
    bits.write_field(offset, kNumOffsetBits);

    // write out the length, minus the break even point
    const std::uint32_t final_length = length - (kBreakEvenPoint + 1);
    bits.write_field(final_length, kNumLengthBits);

    return true;
}

bool LzssWindow::write_token(BitWriter& bits)
{
    std::uint32_t match_pos = m_MatchPos;
    std::uint32_t match_len = m_MatchLen;

    // make sure to clamp the value to the actual maximum size of the look ahead buffer
    if (match_len > m_LookAheadLen)
    {
        match_len = m_LookAheadLen;
    }

    // see if we can save space by just printing out bytes
    if (match_len <= kBreakEvenPoint)
    {
        // print the flag indicating that it is just a raw byte
        bits.set_bit(1);

        // now print out the byte
        bits.write_field(m_Window[m_LookAheadPos], 8);

        // set the amount we need to print out
        match_len = 1;
    }
    else
    {
        write_span(bits, match_pos, match_len);
    }

    // shrink the size of the look ahead buffer
    m_LookAheadLen -= match_len;

    return true;
}

//---------------------------------------------------------------------------
// Tree manipulation
//---------------------------------------------------------------------------

void LzssWindow::collapse_node(std::uint32_t node, std::uint32_t child)
{
    // get the parent node
    const std::uint32_t parent = m_Tree[node].m_Parent;

    // see which branch of the parent we will modify
    if (m_Tree[parent].m_Smaller == node)
    {
        m_Tree[parent].m_Smaller = child;
    }
    else
    {
        m_Tree[parent].m_Larger = child;
    }

    // clean up the removed node
    m_Tree[child].m_Parent = parent;
    m_Tree[node].m_Parent  = kNullNode;
}

void LzssWindow::replace_node(std::uint32_t node, std::uint32_t replace_with)
{
    // get the parent
    const std::uint32_t parent = m_Tree[node].m_Parent;

    // link the parent to the new node
    if (m_Tree[parent].m_Larger == node)
    {
        m_Tree[parent].m_Larger = replace_with;
    }
    else
    {
        m_Tree[parent].m_Smaller = replace_with;
    }

    // set up the new node
    m_Tree[replace_with].m_Parent  = m_Tree[node].m_Parent;
    m_Tree[replace_with].m_Larger  = m_Tree[node].m_Larger;
    m_Tree[replace_with].m_Smaller = m_Tree[node].m_Smaller;

    // parentize the children correctly
    m_Tree[m_Tree[replace_with].m_Smaller].m_Parent = replace_with;
    m_Tree[m_Tree[replace_with].m_Larger].m_Parent  = replace_with;

    // mark the old node as invalid
    m_Tree[node].m_Parent = kNullNode;
}

void LzssWindow::delete_node(std::uint32_t node)
{
    if ((m_Tree[node].m_Parent == kNullNode) || (node == kNullNode))
    {
        return;
    }

    // first check to see if we can simply collapse the node (only one child)
    if (m_Tree[node].m_Larger == kNullNode)
    {
        collapse_node(node, m_Tree[node].m_Smaller);
    }
    else if (m_Tree[node].m_Smaller == kNullNode)
    {
        collapse_node(node, m_Tree[node].m_Larger);
    }
    else
    {
        // find the one to replace it with
        std::uint32_t replace_with = m_Tree[node].m_Smaller;

        // continually move down the larger side until we have to stop
        while (m_Tree[replace_with].m_Larger != kNullNode)
        {
            replace_with = m_Tree[replace_with].m_Larger;
        }

        // clean up that node
        collapse_node(replace_with, m_Tree[replace_with].m_Smaller);
        // now we swap the two nodes
        replace_node(node, replace_with);
    }
}

void LzssWindow::add_node(std::uint32_t window_node, std::uint32_t& match_pos,
                          std::uint32_t& match_len)
{
    // init the match to something invalid
    match_len = 0;
    match_pos = kNullNode;

    // see if we are on a valid node
    if (window_node == kNullNode)
    {
        return;
    }

    // start out at the first node (stored in the root's larger branch)
    std::uint32_t curr_node = m_Tree[kRootNode].m_Larger;

    // pointer to the window where we will be comparing
    const std::uint8_t* window_pos_ptr = m_Window + window_node;

    // the strcmp return value essentially
    std::int32_t delta;

    // count up the match
    std::uint32_t curr_match_len;

    // now we run through the tree moving appropriately, counting the match lengths
    while (true)
    {
        // reset the match count
        curr_match_len = 0;
        const std::uint8_t* curr_ptr = m_Window + curr_node;

        do
        {
            delta = static_cast<std::int32_t>(window_pos_ptr[curr_match_len]) -
                    static_cast<std::int32_t>(curr_ptr[curr_match_len]);

            // see if the strings are not equal
            if (delta != 0)
            {
                break;
            }

            // matched another character
            ++curr_match_len;

            // unroll the loop once
            delta = static_cast<std::int32_t>(window_pos_ptr[curr_match_len]) -
                    static_cast<std::int32_t>(curr_ptr[curr_match_len]);

            if (delta != 0)
            {
                break;
            }

            // matched another character
            ++curr_match_len;

        } while (curr_match_len < kLookAheadSize);

        // see if this match is the best one so far
        if (curr_match_len > match_len)
        {
            if (curr_match_len >= kLookAheadSize)
            {
                // save the settings
                match_len = kLookAheadSize;
                match_pos = curr_node;

                // since we have an exact match, we can simply replace the old node with
                // this one (since it would be pulled out of the buffer first) which
                // prevents buffer clutter
                replace_node(curr_node, window_node);

                // bail, since we couldn't possibly find a better match
                return;
            }

            // save the settings
            match_len = curr_match_len;
            match_pos = curr_node;
        }

        // see where we need to travel next
        std::uint32_t* next_node = (delta > 0) ? &(m_Tree[curr_node].m_Larger)
                                               : &(m_Tree[curr_node].m_Smaller);

        // now we need to see if we hit a dead end
        if (*next_node == kNullNode)
        {
            // found a dead end, insert our node here
            *next_node = window_node;

            // update the new node's links
            m_Tree[window_node].m_Parent  = curr_node;
            m_Tree[window_node].m_Larger  = kNullNode;
            m_Tree[window_node].m_Smaller = kNullNode;

            return;
        }

        curr_node = *next_node;
    }
}

} // namespace ltc
