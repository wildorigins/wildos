#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

class QProcess;

// Keyboard layouts, the key that switches between them, Caps Lock, the
// compose and Alt Gr keys, the keyboard model and key repeat, kept where
// KDE's keyboard module keeps them: the layouts, model and XKB options in
// kxkbrc (KWin rereads it on org.kde.keyboard reloadConfig) and repeat and
// Num Lock in kcminputrc, which KWin watches.
//
// A layout can also carry a typing method, as Windows' Vietnamese Telex
// keyboard does: the keys are an ordinary layout's (US English for Telex),
// and Fcitx 5 turns letters into Vietnamese while that entry is the one in
// use. The method is kept beside the layout in kxkbrc (WildOSInputMethods,
// one per layout) so moving and switching treat it as a layout like any
// other; wildos-access starts Fcitx and follows the switches. Fcitx is
// downloaded the first time a method is added.
class Keyboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString switchKeys READ switchKeys NOTIFY changed)
    Q_PROPERTY(QString capsLock READ capsLock NOTIFY changed)
    Q_PROPERTY(QString compose READ compose NOTIFY changed)
    Q_PROPERTY(QString altGr READ altGr NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(QString modelName READ modelName NOTIFY changed)
    Q_PROPERTY(int numLock READ numLock NOTIFY changed)
    Q_PROPERTY(bool repeat READ repeat NOTIFY changed)
    Q_PROPERTY(int repeatDelay READ repeatDelay NOTIFY changed)
    Q_PROPERTY(int repeatRate READ repeatRate NOTIFY changed)
    // A layout with a typing method whose download hasn't happened yet.
    Q_PROPERTY(bool methodMissing READ methodMissing NOTIFY methodChanged)
    // Some layout has a typing method, so Fcitx has the on-screen
    // keyboard's place in KWin.
    Q_PROPERTY(bool methodInUse READ methodInUse NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY methodChanged)
    Q_PROPERTY(QString downloadStep READ downloadStep NOTIFY methodChanged)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY methodChanged)

public:
    explicit Keyboard(QObject *parent = nullptr);
    ~Keyboard() override;

    QVariantList layouts() const;
    int current() const { return m_current; }
    QString switchKeys() const;
    QString capsLock() const;
    QString compose() const;
    QString altGr() const;
    QString model() const { return m_model; }
    QString modelName() const;
    int numLock() const { return m_numLock; }
    bool repeat() const { return m_repeat; }
    int repeatDelay() const { return m_repeatDelay; }
    int repeatRate() const { return m_repeatRate; }
    bool methodMissing() const;
    bool methodInUse() const;
    bool downloading() const { return m_process != nullptr; }
    QString downloadStep() const { return m_step; }
    QString downloadError() const { return m_error; }

    // Every layout and variant XKB knows, as { layout, variant, method,
    // name }, sorted by name, with the typing methods among them. Read once,
    // on first use.
    Q_INVOKABLE QVariantList available();
    // Every keyboard model XKB knows, as { model, name }, sorted by name.
    Q_INVOKABLE QVariantList models();
    Q_INVOKABLE void add(const QString &layout, const QString &variant, const QString &method = QString());
    // Fetches what the typing methods in the list need.
    Q_INVOKABLE void download();
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void use(int index);
    Q_INVOKABLE void setSwitchKeys(const QString &option);
    Q_INVOKABLE void setCapsLock(const QString &option);
    Q_INVOKABLE void setCompose(const QString &option);
    Q_INVOKABLE void setAltGr(const QString &option);
    Q_INVOKABLE void setModel(const QString &model);
    Q_INVOKABLE void setNumLock(int state);
    Q_INVOKABLE void setRepeat(bool on);
    Q_INVOKABLE void setRepeatDelay(int ms);
    Q_INVOKABLE void setRepeatRate(int perSecond);
    Q_INVOKABLE void reload();

Q_SIGNALS:
    void changed();
    void currentChanged();
    void methodChanged();

private Q_SLOTS:
    void onLayoutChanged(uint index);
    void readCurrent();

private:
    struct Layout {
        QString layout;
        QString variant;
        QString method; // telex or vni, or empty for a plain layout
    };

    void loadNames();
    QString nameOf(const Layout &l) const;
    void saveLayouts();
    void saveRepeat();
    QString optionWithPrefix(const QStringList &prefixes) const;
    void replaceOptions(const QStringList &prefixes, const QString &option);

    QProcess *m_process = nullptr;
    QString m_step;
    QString m_error;

    QList<Layout> m_layouts;
    QStringList m_options;
    QString m_model;
    int m_current = 0;
    int m_numLock = 2;
    bool m_repeat = true;
    int m_repeatDelay = 600;
    int m_repeatRate = 25;

    bool m_namesLoaded = false;
    QHash<QString, QString> m_names;
    QVariantList m_available;
    QHash<QString, QString> m_modelNames;
    QVariantList m_models;
};
