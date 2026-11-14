#pragma once

#include "dbustypes.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

#include "dirmodel.h"

// One Open or Save dialog: what the app asked for (title, mode, file
// types, extra choices, where to start, the suggested name) and the rules
// for turning what was typed and picked into the files handed back. The
// window (FileChooser.qml) draws it; this decides.
class Chooser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by the portal for each request")

    Q_PROPERTY(QString mode READ mode CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString acceptLabel READ acceptLabel CONSTANT)
    Q_PROPERTY(QString appIcon READ appIcon CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(DirModel *folder READ folder CONSTANT)
    Q_PROPERTY(QVariantList filters READ filters CONSTANT)
    Q_PROPERTY(int currentFilter READ currentFilter WRITE setCurrentFilter NOTIFY currentFilterChanged)
    Q_PROPERTY(QVariantList choices READ choices NOTIFY choicesChanged)
    Q_PROPERTY(QString initialName READ initialName CONSTANT)
    Q_PROPERTY(QString nameLabel READ nameLabel CONSTANT)
    Q_PROPERTY(QString typeLabel READ typeLabel CONSTANT)
    // "details" or "icons" (Large icons), remembered per kind of file.
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)

public:
    enum Mode { Open, Save, SaveFiles, Folder };

    struct Request {
        Mode mode = Open;
        QString title;
        QString acceptLabel;
        QString appId;
        bool multiple = false;
        Portal::Filters filters;
        int currentFilter = -1;
        Portal::Choices choices;
        QUrl folder;
        QString name;
        QStringList files; // SaveFiles: the names to be saved
    };

    explicit Chooser(const Request &request, QObject *parent = nullptr);

    QString mode() const;
    QString title() const;
    QString acceptLabel() const;
    QString appIcon() const;
    bool multiple() const { return m_request.multiple; }
    DirModel *folder() const { return m_folder; }
    QVariantList filters() const;
    int currentFilter() const { return m_currentFilter; }
    void setCurrentFilter(int index);
    QVariantList choices() const;
    QString initialName() const { return m_initialName; }
    QString nameLabel() const;
    QString typeLabel() const;
    QString viewMode() const { return m_viewMode; }
    void setViewMode(const QString &mode);

    // What the name field holds once a file is picked in the list.
    Q_INVOKABLE QString nameForSelection() const;
    Q_INVOKABLE void setChoice(const QString &id, const QString &value);
    // The "All files" type, or -1 when the app offers none.
    Q_INVOKABLE int allFilesIndex() const;
    // The name with the extension of type `from` changed to that of `to`,
    // so the name box never shows one type while saving as another.
    Q_INVOKABLE QString swapSuffix(const QString &name, int from, int to) const;
    // Where the name's extension starts, or -1, so opening Save selects
    // only the name, as Windows does.
    Q_INVOKABLE int suffixStart(const QString &name) const;

    // The main button, Enter in the name field, or a double click on a
    // file. Returns "" when done (or when it moved into a folder instead),
    // "replace:<name>" when a file would be overwritten and the person must
    // agree first, or a sentence to show when it cannot go ahead.
    Q_INVOKABLE QString accept(const QString &typed);
    Q_INVOKABLE void confirmReplace();
    Q_INVOKABLE void cancel();

    // The portal's answer: 0 chosen, 1 cancelled, 2 ended another way.
    Q_SIGNAL void finished(uint response, const QVariantMap &results);

Q_SIGNALS:
    void currentFilterChanged();
    void choicesChanged();
    void viewModeChanged();

private:
    QString suffixFor(int filter) const;
    bool hasKnownSuffix(const QString &name) const;
    QString displayName(const QString &fileName) const;
    QString finalName(const QString &typed) const;
    void finish(const QList<QUrl> &urls);
    QString recentKey() const;
    QUrl startFolder() const;
    void rememberFolder(const QList<QUrl> &urls);

    Request m_request;
    DirModel *m_folder;
    int m_currentFilter = -1;
    QHash<QString, QString> m_chosen;
    QString m_initialName;
    QString m_purpose;
    QString m_viewMode;
    QList<QUrl> m_pending;
    bool m_done = false;
};
