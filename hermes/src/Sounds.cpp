#include "Sounds.hpp"

#include "Notifications.hpp"

#include <canberra.h>

namespace hermes {
namespace {

// Notifications arriving together sound once.
constexpr qint64 Quiet = 500;

} // namespace

void Sounds::ContextDeleter::operator()(ca_context* context) const
{
    ca_context_destroy(context);
}

Sounds::Sounds()
{
    ca_context* context = nullptr;
    if (ca_context_create(&context) != CA_SUCCESS) {
        qWarning("tde-hermes: no sound for notifications");
        return;
    }
    m_context.reset(context);
    ca_context_change_props(m_context.get(), CA_PROP_APPLICATION_NAME, "Hermes", CA_PROP_APPLICATION_ID, "tde-hermes",
        CA_PROP_CANBERRA_XDG_THEME_NAME, "freedesktop", nullptr);
}

Sounds::~Sounds() = default;

void Sounds::play(const Notification& notification)
{
    if (!m_context || (m_last.isValid() && m_last.elapsed() < Quiet))
        return;
    const QString sound = notification.sound();
    if (sound.isEmpty())
        return;
    const QByteArray value = sound.toUtf8();
    const int result = sound.startsWith(u'/')
        ? ca_context_play(m_context.get(), 0, CA_PROP_MEDIA_FILENAME, value.constData(), CA_PROP_EVENT_DESCRIPTION,
              "Notification", nullptr)
        : ca_context_play(m_context.get(), 0, CA_PROP_EVENT_ID, value.constData(), CA_PROP_EVENT_DESCRIPTION,
              "Notification", nullptr);
    if (result != CA_SUCCESS)
        qWarning("tde-hermes: could not play the notification sound: %s", ca_strerror(result));
    m_last.start();
}

} // namespace hermes
