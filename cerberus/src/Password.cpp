#include "Password.hpp"

#include <cstring>

namespace cerberus {
namespace {

bool continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

} // namespace

bool Password::append(std::string_view text)
{
    // Room for the zero byte at the end.
    if (m_size + text.size() >= m_buffer.size())
        return false;
    std::memcpy(m_buffer.data() + m_size, text.data(), text.size());
    m_size += text.size();
    m_buffer[m_size] = '\0';
    return true;
}

void Password::removeLast()
{
    while (m_size > 0 && continuation(m_buffer[m_size - 1]))
        m_buffer[--m_size] = '\0';
    if (m_size > 0)
        m_buffer[--m_size] = '\0';
}

void Password::clear()
{
    explicit_bzero(m_buffer.data(), m_buffer.size());
    m_size = 0;
}

std::size_t Password::length() const
{
    std::size_t characters = 0;
    for (std::size_t i = 0; i < m_size; ++i) {
        if (!continuation(m_buffer[i]))
            ++characters;
    }
    return characters;
}

} // namespace cerberus
