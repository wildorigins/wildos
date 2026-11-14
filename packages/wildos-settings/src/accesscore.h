#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

// Where every accessibility choice lives and how the desktop is told about
// it, shared by Settings' Accessibility page and wildos-access (the
// shortcuts in the session, on the lock screen and on the sign-in screen),
// so a switch and its shortcut can never disagree. Each value is kept where
// KDE keeps it, so Plasma's own modules and KWin read what is written here:
//
//   kaccessrc   sticky, slow and bounce keys, the visible bell and the
//               screen reader, as kcm_access writes them; KWin's plugins
//               watch the file, so every write carries KConfig::Notify
//   kdeglobals  the high contrast colour scheme
//   kwinrc      the zoom effect, the on-screen keyboard, and the window
//               outline and background blur effects with the blur's
//               settings
//   kcminputrc  the pointer theme and size
//   wildosrc    what the lock and sign-in screens need to know, the theme
//               to go back to when high contrast is turned off, and
//               stronger text on glass
namespace AccessCore
{
// Wild OS's high contrast scheme (wildos-branding).
QString highContrastScheme();

bool stickyKeys();
void setStickyKeys(bool on);
bool slowKeys();
int slowKeysDelay();
void setSlowKeys(bool on);
void setSlowKeysDelay(int ms);
bool bounceKeys();
int bounceKeysDelay();
void setBounceKeys(bool on);
void setBounceKeysDelay(int ms);
bool visibleBell();
void setVisibleBell(bool on);

bool screenReaderInstalled();
bool screenReader();
void setScreenReader(bool on);
// Starts Orca if it was left on, for a screen with no kaccess of its own
// (the sign-in screen).
void restoreScreenReader();

bool highContrast();
// plasma-apply-colorscheme runs in the background; done is called on
// context's thread once the scheme (and the accent, when going back) is in.
void setHighContrast(bool on, QObject *context, const std::function<void()> &done = {});

// "zoom" in KWin's active effects means the screen is magnified now.
bool magnifier();
void setMagnifier(bool on);
// in, out or reset: KWin's own zoom actions, so they behave exactly like
// Super and plus, minus and 0.
void zoom(const QString &action);

// The input method KWin runs as its on-screen keyboard: any installed
// keyboard that declares itself one (X-KDE-Wayland-VirtualKeyboard), Maliit
// first.
QString keyboardDesktopFile();
bool onScreenKeyboard();
void setOnScreenKeyboard(bool on, QObject *context, const std::function<void()> &done = {});

// The pointer colours Settings offers, as { id, name, colour, theme }.
QVariantList pointerColours();
// "custom" while the pointer is the account's own (customPointerTheme).
QString pointerColour();
// The account's own pointer, made by pointer-import from a Windows pointer
// or a picture, in ~/.local/share/icons/WildOS-Custom.
QString customPointerTheme();
QString customPointerDir();
int pointerSize();
QList<int> pointerSizes();
void setPointer(const QString &colourId, int size);

bool monoAudio();
void setMonoAudio(bool on);

// wildos-shell's KWin effects, loaded and unloaded at once and kept for
// the next sign-in. Supported is false where KWin draws without OpenGL.
bool windowEffectsInstalled();
bool windowEffectsSupported();
bool outlineWindows();
void setOutlineWindows(bool on);
// Whether maximised windows, and others that fill the screen, are outlined.
bool outlineFillingScreen();
void setOutlineFillingScreen(bool on);
bool blurBackground();
void setBlurBackground(bool on);
// 1 (lightest) to 5.
int blurStrength();
void setBlurStrength(int step);
bool blurDesktop();
void setBlurDesktop(bool on);

// Glass behind text (the taskbar, Start, the flyouts, Wild OS apps) made
// nearly solid and muted text brighter. WildOS.UI follows the key, and
// wildos-auto-theme puts on the solid Plasma style that matches the theme.
bool strongerText();
void setStrongerText(bool on);
}
