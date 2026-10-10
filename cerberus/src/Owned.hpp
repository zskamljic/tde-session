#pragma once

#include <unistd.h>

#include <memory>
#include <utility>

// What the lock screen holds of libraries and of the system, let go of along with its owner.
namespace cerberus {

// An object of a C library, destroyed with the function it provides.
template <typename T, auto Destroy> struct Deleter {
    void operator()(T* object) const { Destroy(object); }
};
template <typename T, auto Destroy> using Owned = std::unique_ptr<T, Deleter<T, Destroy>>;

// A file descriptor, closed along with its owner.
class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd)
        : m_fd(fd)
    {
    }
    ~FileDescriptor() { reset(); }

    FileDescriptor(FileDescriptor&& other) noexcept
        : m_fd(std::exchange(other.m_fd, -1))
    {
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }

    int get() const { return m_fd; }
    explicit operator bool() const { return m_fd >= 0; }

    void reset()
    {
        if (m_fd >= 0)
            close(m_fd);
        m_fd = -1;
    }

private:
    int m_fd = -1;
};

} // namespace cerberus
