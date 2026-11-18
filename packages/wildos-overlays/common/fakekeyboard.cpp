#include "fakekeyboard.h"

#include "qwayland-fake-input.h"

#include <QGuiApplication>
#include <QThread>
#include <QWaylandClientExtensionTemplate>
#include <wayland-client.h>

namespace
{
// Linux input event codes (linux/input-event-codes.h).
constexpr uint32_t keyLeftCtrl = 29;
constexpr uint32_t keyLeftShift = 42;
constexpr uint32_t keyV = 47;
constexpr uint32_t pressed = 1;
constexpr uint32_t released = 0;
constexpr int keyGapMs = 20;
}

class FakeInput : public QWaylandClientExtensionTemplate<FakeInput>, public QtWayland::org_kde_kwin_fake_input
{
public:
    FakeInput()
        : QWaylandClientExtensionTemplate<FakeInput>(4)
    {
        initialize();
    }

    ~FakeInput() override
    {
        if (isActive()) {
            destroy();
        }
    }

    bool authenticated = false;
};

FakeKeyboard::FakeKeyboard(QObject *parent)
    : QObject(parent)
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        m_input = std::make_unique<FakeInput>();
    }
}

FakeKeyboard::~FakeKeyboard() = default;

bool FakeKeyboard::available() const
{
    return m_input && m_input->isActive();
}

bool FakeKeyboard::paste(bool shift)
{
    if (!available()) {
        return false;
    }
    if (!m_input->authenticated) {
        m_input->authenticate(QStringLiteral("Clipboard and emoji"), QStringLiteral("Paste what you picked into the app you are using"));
        m_input->authenticated = true;
    }
    // One step at a time, each sent and given a moment. Sent in one go, an
    // input method (Vietnamese, Chinese, Japanese and Korean typing all use
    // one) passed the V on before it had heard about Ctrl, and the app got
    // the letter v.
    const auto step = [this](uint32_t key, uint32_t state) {
        m_input->keyboard_key(key, state);
        if (auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            wl_display_flush(app->display());
        }
        QThread::msleep(keyGapMs);
    };
    step(keyLeftCtrl, pressed);
    if (shift) {
        step(keyLeftShift, pressed);
    }
    step(keyV, pressed);
    step(keyV, released);
    if (shift) {
        step(keyLeftShift, released);
    }
    step(keyLeftCtrl, released);
    return true;
}
