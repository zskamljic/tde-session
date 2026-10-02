#pragma once

#include <QElapsedTimer>

#include <memory>

struct ca_context;

namespace hermes {

struct Notification;

// Plays the sound of a notification as it arrives, from the desktop's sound theme, through
// libcanberra.
class Sounds {
public:
    Sounds();
    ~Sounds();

    void play(const Notification& notification);

private:
    struct ContextDeleter {
        void operator()(ca_context* context) const;
    };

    std::unique_ptr<ca_context, ContextDeleter> m_context;
    QElapsedTimer m_last;
};

} // namespace hermes
