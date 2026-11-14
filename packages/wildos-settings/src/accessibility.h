#pragma once

#include <KConfigWatcher>

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

// Settings > Accessibility. The values and how they are applied live in
// AccessCore, which the shortcut service shares; this follows them live,
// so a shortcut pressed while the page is open (Super Alt H, Shift five
// times) moves its switch too, and a page made again by the Loader reads
// the current state rather than anything remembered from last time.
class Accessibility : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList pointerColours READ pointerColours CONSTANT)
    Q_PROPERTY(QList<int> pointerSizes READ pointerSizes CONSTANT)
    Q_PROPERTY(QString pointerColour READ pointerColour NOTIFY changed)
    Q_PROPERTY(int pointerSize READ pointerSize NOTIFY changed)
    // The account's own pointer: whether there is one, its picture for the
    // swatch, the file it was made from, and whether one is being made.
    Q_PROPERTY(bool customPointer READ customPointer NOTIFY customPointerChanged)
    Q_PROPERTY(QUrl customPointerPreview READ customPointerPreview NOTIFY customPointerChanged)
    Q_PROPERTY(QString customPointerSource READ customPointerSource NOTIFY customPointerChanged)
    Q_PROPERTY(bool pointerBusy READ pointerBusy NOTIFY customPointerChanged)
    Q_PROPERTY(bool magnifier READ magnifier NOTIFY magnifierChanged)
    Q_PROPERTY(bool screenReaderInstalled READ screenReaderInstalled CONSTANT)
    Q_PROPERTY(bool screenReader READ screenReader NOTIFY changed)
    Q_PROPERTY(bool highContrastInstalled READ highContrastInstalled CONSTANT)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY changed)
    // True while the colour scheme is being applied, which takes a moment.
    Q_PROPERTY(bool highContrastBusy READ highContrastBusy NOTIFY changed)
    Q_PROPERTY(bool visibleBell READ visibleBell NOTIFY changed)
    Q_PROPERTY(bool monoAudioInstalled READ monoAudioInstalled CONSTANT)
    Q_PROPERTY(bool monoAudio READ monoAudio NOTIFY changed)
    Q_PROPERTY(bool onScreenKeyboardInstalled READ onScreenKeyboardInstalled CONSTANT)
    Q_PROPERTY(bool onScreenKeyboard READ onScreenKeyboard NOTIFY changed)
    Q_PROPERTY(bool stickyKeys READ stickyKeys NOTIFY changed)
    Q_PROPERTY(bool slowKeys READ slowKeys NOTIFY changed)
    Q_PROPERTY(int slowKeysDelay READ slowKeysDelay NOTIFY changed)
    Q_PROPERTY(bool bounceKeys READ bounceKeys NOTIFY changed)
    Q_PROPERTY(int bounceKeysDelay READ bounceKeysDelay NOTIFY changed)
    Q_PROPERTY(bool windowEffectsInstalled READ windowEffectsInstalled CONSTANT)
    Q_PROPERTY(bool windowEffectsSupported READ windowEffectsSupported CONSTANT)
    Q_PROPERTY(bool outlineWindows READ outlineWindows NOTIFY changed)
    Q_PROPERTY(bool outlineFillingScreen READ outlineFillingScreen NOTIFY changed)
    Q_PROPERTY(bool blurBackground READ blurBackground NOTIFY changed)
    Q_PROPERTY(int blurStrength READ blurStrength NOTIFY changed)
    Q_PROPERTY(bool blurDesktop READ blurDesktop NOTIFY changed)
    Q_PROPERTY(bool strongerText READ strongerText NOTIFY changed)

public:
    explicit Accessibility(QObject *parent = nullptr);

    QVariantList pointerColours() const { return m_pointerColours; }
    QList<int> pointerSizes() const;
    QString pointerColour() const { return m_pointerColour; }
    int pointerSize() const { return m_pointerSize; }
    bool customPointer() const { return m_customPointer; }
    QUrl customPointerPreview() const { return m_customPreview; }
    QString customPointerSource() const { return m_customSource; }
    bool pointerBusy() const { return m_pointerBusy; }
    bool magnifier() const { return m_magnifier; }
    bool screenReaderInstalled() const { return m_screenReaderInstalled; }
    bool screenReader() const { return m_screenReader; }
    bool highContrastInstalled() const { return m_highContrastInstalled; }
    bool highContrast() const { return m_highContrast; }
    bool highContrastBusy() const { return m_highContrastBusy; }
    bool visibleBell() const { return m_visibleBell; }
    bool monoAudioInstalled() const { return m_monoAudioInstalled; }
    bool monoAudio() const { return m_monoAudio; }
    bool onScreenKeyboardInstalled() const { return m_keyboardInstalled; }
    bool onScreenKeyboard() const { return m_onScreenKeyboard; }
    bool stickyKeys() const { return m_stickyKeys; }
    bool slowKeys() const { return m_slowKeys; }
    int slowKeysDelay() const { return m_slowKeysDelay; }
    bool bounceKeys() const { return m_bounceKeys; }
    int bounceKeysDelay() const { return m_bounceKeysDelay; }
    bool windowEffectsInstalled() const { return m_windowEffectsInstalled; }
    bool windowEffectsSupported() const { return m_windowEffectsSupported; }
    bool outlineWindows() const { return m_outlineWindows; }
    bool outlineFillingScreen() const { return m_outlineFillingScreen; }
    bool blurBackground() const { return m_blurBackground; }
    int blurStrength() const { return m_blurStrength; }
    bool blurDesktop() const { return m_blurDesktop; }
    bool strongerText() const { return m_strongerText; }

    Q_INVOKABLE void setPointerColour(const QString &id);
    Q_INVOKABLE void setPointerSize(int size);
    // Makes the account's own pointer from a .cur, .ani, a pointer pack
    // (.zip or its install.inf) or a .png or .svg picture, whose hotspot is
    // given as a fraction of its width and height, and switches to it.
    Q_INVOKABLE void addPointer(const QUrl &file, qreal hotX = 0, qreal hotY = 0);
    Q_INVOKABLE void removeCustomPointer();
    Q_INVOKABLE void setMagnifier(bool on);
    Q_INVOKABLE void setScreenReader(bool on);
    Q_INVOKABLE void setHighContrast(bool on);
    Q_INVOKABLE void setVisibleBell(bool on);
    Q_INVOKABLE void setMonoAudio(bool on);
    Q_INVOKABLE void setOnScreenKeyboard(bool on);
    Q_INVOKABLE void setStickyKeys(bool on);
    Q_INVOKABLE void setSlowKeys(bool on);
    Q_INVOKABLE void setSlowKeysDelay(int ms);
    Q_INVOKABLE void setBounceKeys(bool on);
    Q_INVOKABLE void setBounceKeysDelay(int ms);
    Q_INVOKABLE void setOutlineWindows(bool on);
    Q_INVOKABLE void setOutlineFillingScreen(bool on);
    Q_INVOKABLE void setBlurBackground(bool on);
    Q_INVOKABLE void setBlurStrength(int step);
    Q_INVOKABLE void setBlurDesktop(bool on);
    Q_INVOKABLE void setStrongerText(bool on);

public Q_SLOTS:
    // Rereads everything. KWin has no signal for zoom, so the page also
    // calls refreshMagnifier while it is showing.
    void refresh();
    void refreshMagnifier();

Q_SIGNALS:
    void changed();
    void magnifierChanged();
    void customPointerChanged();
    void pointerFailed(const QString &message);

private:
    void watch(const QString &file);
    void readCustomPointer();
    static QString x11Link();
    static void linkForX11();

    QVariantList m_pointerColours;
    QString m_pointerColour;
    int m_pointerSize = 24;
    bool m_customPointer = false;
    QUrl m_customPreview;
    QString m_customSource;
    bool m_pointerBusy = false;
    bool m_magnifier = false;
    bool m_screenReaderInstalled = false;
    bool m_screenReader = false;
    bool m_highContrastInstalled = false;
    bool m_highContrast = false;
    bool m_highContrastBusy = false;
    bool m_visibleBell = false;
    bool m_monoAudioInstalled = false;
    bool m_monoAudio = false;
    bool m_keyboardInstalled = false;
    bool m_onScreenKeyboard = false;
    bool m_stickyKeys = false;
    bool m_slowKeys = false;
    int m_slowKeysDelay = 500;
    bool m_bounceKeys = false;
    int m_bounceKeysDelay = 500;
    bool m_windowEffectsInstalled = false;
    bool m_windowEffectsSupported = true;
    bool m_outlineWindows = false;
    bool m_outlineFillingScreen = true;
    bool m_blurBackground = false;
    int m_blurStrength = 3;
    bool m_blurDesktop = true;
    bool m_strongerText = false;

    QList<KConfigWatcher::Ptr> m_watchers;
};
