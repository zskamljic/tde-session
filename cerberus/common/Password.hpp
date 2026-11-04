#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cerberus {

// The password as it is typed, kept in one fixed place and wiped when done with, so no
// copies of it are left lying around in memory.
class Password {
public:
    Password() = default;
    ~Password() { clear(); }

    Password(const Password&) = delete;
    Password& operator=(const Password&) = delete;

    // Adds text typed, in UTF-8; false when the password is as long as it may be.
    bool append(std::string_view text);
    // Takes away the last character typed, which may be several bytes.
    void removeLast();
    void clear();

    // How many characters were typed, for the dots that stand for them.
    std::size_t length() const;
    bool empty() const { return m_size == 0; }
    // Ends in a zero byte, as C functions like PAM want it.
    const char* data() const { return m_buffer.data(); }

private:
    std::array<char, 1024> m_buffer {};
    std::size_t m_size = 0;
};

} // namespace cerberus
