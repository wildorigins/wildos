#pragma once

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Everything the start screen knows: which app it is (Write, Tables or
// Slides),
// the templates, the documents LibreOffice opened lately and the ones the
// person pinned, and starting LibreOffice on whichever is picked.
class Office : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool tables READ tables CONSTANT)
    Q_PROPERTY(bool slides READ slides CONSTANT)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString iconName READ iconName CONSTANT)
    Q_PROPERTY(QString formatName READ formatName CONSTANT)
    Q_PROPERTY(QString formatNote READ formatNote CONSTANT)
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)
    Q_PROPERTY(QString documentsFolder READ documentsFolder CONSTANT)
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)
    Q_PROPERTY(QVariantList userTemplates READ userTemplates NOTIFY changed)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY resultsChanged)

public:
    enum App { Write, Tables, Slides };
    static void setApp(App app);
    static App app();
    // LibreOffice's switch for this app's kind of document.
    static QString moduleSwitch();
    static QString soffice();
    // Whether this account already has LibreOffice running.
    static bool running();
    // Gives new documents the spelling language chosen in Settings > Typing.
    static void followSpellingLanguage();

    explicit Office(QObject *parent = nullptr);

    bool tables() const { return s_app == Tables; }
    bool slides() const { return s_app == Slides; }
    QString appName() const;
    QString iconName() const;
    QString formatName() const;
    QString formatNote() const;
    QStringList nameFilters() const;
    QString documentsFolder() const;
    QVariantList templates() const { return m_templates; }
    QVariantList userTemplates() const { return m_userTemplates; }
    QVariantList recent() const { return m_recent; }
    QVariantList pinned() const { return m_pinned; }
    QVariantList results() const { return m_results; }
    bool searching() const { return m_searching; }

    // Each starts LibreOffice and returns whether it did; the window then
    // waits for the document's window to take over before it goes.
    Q_INVOKABLE bool isLibreOfficeRunning() const;
    Q_INVOKABLE bool open(const QString &pathOrUrl);
    Q_INVOKABLE bool openTemplate(const QString &path);
    Q_INVOKABLE bool newBlank();

    Q_INVOKABLE void showInFolder(const QString &path);
    Q_INVOKABLE void pin(const QString &path);
    Q_INVOKABLE void unpin(const QString &path);
    Q_INVOKABLE void forget(const QString &path);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void search(const QString &text);

    // Today, 09:58 / Yesterday / Sat / 04/11/2026.
    static QString whenText(const QDateTime &time);
    // Documents > Rowing club.
    static QString folderText(const QString &directory);

Q_SIGNALS:
    void changed();
    void resultsChanged();
    void failed(const QString &message);

private:
    bool launch(const QStringList &arguments);
    bool ownsFile(const QString &path) const;
    QVariantMap describe(const QString &path) const;
    QStringList pickList() const;
    QString dataDir() const;
    void loadStore();
    void saveStore() const;
    void loadTemplates();
    void removeFromPickList(const QString &url) const;
    void finishSearch(int generation, const QStringList &found);

    QVariantList m_templates;
    QVariantList m_userTemplates;
    QVariantList m_recent;
    QVariantList m_pinned;
    QVariantList m_results;
    QStringList m_pins;
    QHash<QString, qint64> m_hidden;
    bool m_searching = false;
    int m_searchGeneration = 0;
    QString m_query;

    static App s_app;
};
