#include "gamecontrollers.h"

#include <SDL2/SDL.h>

#include <algorithm>

namespace
{
// Often enough that a tap of a button shows.
const int pollInterval = 30;

struct Named {
    SDL_GameControllerButton button;
    const char *name;
};

// Windows' controller test names the buttons after an Xbox controller,
// which is also the layout SDL maps every known gamepad to.
const Named gamepadButtons[] = {
    {SDL_CONTROLLER_BUTTON_A, "A"},
    {SDL_CONTROLLER_BUTTON_B, "B"},
    {SDL_CONTROLLER_BUTTON_X, "X"},
    {SDL_CONTROLLER_BUTTON_Y, "Y"},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "LB"},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "RB"},
    {SDL_CONTROLLER_BUTTON_BACK, QT_TRANSLATE_NOOP("GameControllers", "View")},
    {SDL_CONTROLLER_BUTTON_START, QT_TRANSLATE_NOOP("GameControllers", "Menu")},
    {SDL_CONTROLLER_BUTTON_GUIDE, QT_TRANSLATE_NOOP("GameControllers", "Home")},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, QT_TRANSLATE_NOOP("GameControllers", "Left stick")},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, QT_TRANSLATE_NOOP("GameControllers", "Right stick")},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, QT_TRANSLATE_NOOP("GameControllers", "Up")},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, QT_TRANSLATE_NOOP("GameControllers", "Down")},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, QT_TRANSLATE_NOOP("GameControllers", "Left")},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, QT_TRANSLATE_NOOP("GameControllers", "Right")},
};

struct NamedAxis {
    SDL_GameControllerAxis axis;
    const char *name;
    bool centred;
};

const NamedAxis gamepadAxes[] = {
    {SDL_CONTROLLER_AXIS_LEFTX, QT_TRANSLATE_NOOP("GameControllers", "Left stick, left and right"), true},
    {SDL_CONTROLLER_AXIS_LEFTY, QT_TRANSLATE_NOOP("GameControllers", "Left stick, up and down"), true},
    {SDL_CONTROLLER_AXIS_RIGHTX, QT_TRANSLATE_NOOP("GameControllers", "Right stick, left and right"), true},
    {SDL_CONTROLLER_AXIS_RIGHTY, QT_TRANSLATE_NOOP("GameControllers", "Right stick, up and down"), true},
    {SDL_CONTROLLER_AXIS_TRIGGERLEFT, QT_TRANSLATE_NOOP("GameControllers", "Left trigger"), false},
    {SDL_CONTROLLER_AXIS_TRIGGERRIGHT, QT_TRANSLATE_NOOP("GameControllers", "Right trigger"), false},
};

// SDL's -32768 to 32767 as 0 to 1; a trigger rests at 0.
double position(Sint16 value, bool centred)
{
    const double v = std::clamp(value / 32767.0, -1.0, 1.0);
    return centred ? (v + 1.0) / 2.0 : std::max(0.0, v);
}

QVariantMap axis(const QString &name, double value, bool centred)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("value"), qRound(value * 100) / 100.0}, {QStringLiteral("centred"), centred}};
}

QVariantMap button(const QString &name, bool pressed)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("pressed"), pressed}};
}

QString battery(SDL_Joystick *joystick)
{
    switch (SDL_JoystickCurrentPowerLevel(joystick)) {
    case SDL_JOYSTICK_POWER_FULL:
    case SDL_JOYSTICK_POWER_MAX:
        return QStringLiteral("full");
    case SDL_JOYSTICK_POWER_MEDIUM:
        return QStringLiteral("medium");
    case SDL_JOYSTICK_POWER_LOW:
        return QStringLiteral("low");
    case SDL_JOYSTICK_POWER_EMPTY:
        return QStringLiteral("empty");
    case SDL_JOYSTICK_POWER_WIRED:
        return QStringLiteral("wired");
    default:
        return {};
    }
}
}

GameControllers::GameControllers(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(pollInterval);
    connect(&m_timer, &QTimer::timeout, this, &GameControllers::poll);
}

GameControllers::~GameControllers()
{
    stop();
}

void GameControllers::setActive(bool active)
{
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (active) {
        start();
    } else {
        stop();
    }
    Q_EMIT activeChanged();
}

void GameControllers::start()
{
    if (m_started) {
        return;
    }
    // SDL would otherwise take over SIGINT and SIGTERM from Qt.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    // There is no SDL window to have focus, so input counts regardless.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qWarning("Game controllers: SDL could not start: %s", SDL_GetError());
        return;
    }
    m_started = true;
    // Controllers already plugged in arrive as added events on the first
    // poll.
    poll();
    m_timer.start();
}

void GameControllers::stop()
{
    m_timer.stop();
    if (!m_started) {
        return;
    }
    for (const Device &device : std::as_const(m_devices)) {
        if (device.gamepad) {
            SDL_GameControllerClose(device.gamepad);
        }
        SDL_JoystickClose(device.joystick);
    }
    m_devices.clear();
    SDL_Quit();
    m_started = false;
    if (!m_controllers.isEmpty()) {
        m_controllers.clear();
        Q_EMIT changed();
    }
}

void GameControllers::add(int index)
{
    // Steam Input and similar make virtual copies of real controllers.
    if (SDL_JoystickIsVirtual(index)) {
        return;
    }
    SDL_Joystick *joystick = SDL_JoystickOpen(index);
    if (!joystick) {
        return;
    }
    const int id = SDL_JoystickInstanceID(joystick);
    if (m_devices.contains(id)) {
        SDL_JoystickClose(joystick);
        return;
    }
    Device device;
    device.joystick = joystick;
    if (SDL_IsGameController(index)) {
        device.gamepad = SDL_GameControllerOpen(index);
    }
    m_devices.insert(id, device);
}

void GameControllers::remove(int id)
{
    const auto it = m_devices.constFind(id);
    if (it == m_devices.constEnd()) {
        return;
    }
    if (it->gamepad) {
        SDL_GameControllerClose(it->gamepad);
    }
    SDL_JoystickClose(it->joystick);
    m_devices.erase(it);
}

void GameControllers::poll()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_JOYDEVICEADDED) {
            add(event.jdevice.which);
        } else if (event.type == SDL_JOYDEVICEREMOVED) {
            remove(event.jdevice.which);
        }
    }
    update();
}

void GameControllers::update()
{
    QList<int> ids = m_devices.keys();
    std::sort(ids.begin(), ids.end());

    QVariantList controllers;
    for (const int id : std::as_const(ids)) {
        const Device &device = m_devices[id];
        QVariantList buttons;
        QVariantList axes;
        QString name;
        if (device.gamepad) {
            name = QString::fromUtf8(SDL_GameControllerName(device.gamepad));
            for (const Named &b : gamepadButtons) {
                if (SDL_GameControllerHasButton(device.gamepad, b.button)) {
                    buttons.append(button(tr(b.name), SDL_GameControllerGetButton(device.gamepad, b.button) != 0));
                }
            }
            for (const NamedAxis &a : gamepadAxes) {
                if (SDL_GameControllerHasAxis(device.gamepad, a.axis)) {
                    axes.append(axis(tr(a.name), position(SDL_GameControllerGetAxis(device.gamepad, a.axis), a.centred), a.centred));
                }
            }
        } else {
            // A joystick, wheel or pad SDL has no mapping for: its buttons
            // and axes by number, as the driver reports them.
            name = QString::fromUtf8(SDL_JoystickName(device.joystick));
            for (int i = 0; i < SDL_JoystickNumButtons(device.joystick); ++i) {
                buttons.append(button(QString::number(i + 1), SDL_JoystickGetButton(device.joystick, i) != 0));
            }
            for (int i = 0; i < SDL_JoystickNumAxes(device.joystick); ++i) {
                axes.append(axis(tr("Axis %1").arg(i + 1), position(SDL_JoystickGetAxis(device.joystick, i), true), true));
            }
        }
        if (name.isEmpty()) {
            name = tr("Game controller");
        }
        controllers.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("battery"), battery(device.joystick)},
            {QStringLiteral("buttons"), buttons},
            {QStringLiteral("axes"), axes},
        });
    }

    if (controllers != m_controllers) {
        m_controllers = controllers;
        Q_EMIT changed();
    }
}
