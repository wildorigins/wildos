#pragma once

#include "collector.h"
#include "redactor.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QWindow>

class QNetworkAccessManager;
class QNetworkReply;

// The lines of whatever the preview pane shows. A log line has a time and a
// source; anything else is just text.
class PreviewModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { TimeRole = Qt::UserRole + 1, SourceRole, TextRole, LogRole };
    using QAbstractListModel::QAbstractListModel;

    void setLines(const QStringList &lines, bool log);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    QStringList m_lines;
    bool m_log = false;
};

// The report being written: the answers from the first step, what was
// gathered, what is ticked, the redacted text of each part and the ways of
// sending it. One per window.
class Report : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList parts READ parts CONSTANT)
    Q_PROPERTY(QString part READ part WRITE setPart NOTIFY partChanged)
    Q_PROPERTY(QString partName READ partName NOTIFY partChanged)
    Q_PROPERTY(QString when READ when WRITE setWhen NOTIFY whenChanged)
    Q_PROPERTY(int pickDay READ pickDay WRITE setPickDay NOTIFY whenChanged)
    Q_PROPERTY(QString pickTime READ pickTime WRITE setPickTime NOTIFY whenChanged)
    Q_PROPERTY(QString windowText READ windowText NOTIFY whenChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QString repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)
    Q_PROPERTY(QVariantList screenshots READ screenshots NOTIFY screenshotsChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(QString crashNote READ crashNote NOTIFY crashChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool systemLogs READ systemLogs NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QString viewing READ viewing NOTIFY viewingChanged)
    Q_PROPERTY(QString previewTitle READ previewTitle NOTIFY viewingChanged)
    Q_PROPERTY(PreviewModel *preview READ preview CONSTANT)
    Q_PROPERTY(int removed READ removed NOTIFY redactedChanged)
    Q_PROPERTY(QVariantList removedKinds READ removedKinds NOTIFY redactedChanged)
    Q_PROPERTY(QStringList customWords READ customWords NOTIFY redactedChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY sizeChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString savedText READ savedText NOTIFY savedChanged)
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY savedChanged)
    // After Open GitHub: "opening" while the zip is written and the browser
    // starts, "waiting" while the issue is finished there, "posted" once it
    // shows up on GitHub, empty otherwise.
    Q_PROPERTY(QString handoff READ handoff NOTIFY handoffChanged)
    Q_PROPERTY(bool watching READ watching NOTIFY handoffChanged)
    Q_PROPERTY(QString handoffZip READ handoffZip NOTIFY handoffChanged)
    Q_PROPERTY(int postedNumber READ postedNumber NOTIFY handoffChanged)

public:
    static Report *instance();
    static Report *create(QQmlEngine *, QJSEngine *) { return instance(); }

    // From the command line or a second launch: wildos-report:<part>,
    // --part, --crash/--app after a crash notification, and --event FILE
    // from History.
    void handle(const QStringList &arguments);
    // `--save DIR` from start-up recovery, without a window: builds the
    // whole report and writes it to DIR. Returns the zip's path, or empty.
    QString saveHeadless(const QString &dir, const QString &part, const QDateTime &since, const QDateTime &until, int boot, const QString &description);

    QVariantList parts() const;
    QString part() const { return m_part; }
    void setPart(const QString &part);
    QString partName() const;
    QString when() const { return m_when; }
    void setWhen(const QString &when);
    int pickDay() const { return m_pickDay; }
    void setPickDay(int day);
    QString pickTime() const { return m_pickTime; }
    void setPickTime(const QString &time);
    QString windowText() const;
    QString description() const { return m_description; }
    void setDescription(const QString &text);
    QString repeat() const { return m_repeat; }
    void setRepeat(const QString &repeat);
    QVariantList screenshots() const;
    bool capturing() const { return m_capturing; }
    QString crashNote() const;
    bool busy() const { return m_collecting || m_redacting; }
    bool systemLogs() const { return m_raw.systemLogs; }
    QVariantList items() const;
    QString viewing() const { return m_viewing; }
    QString previewTitle() const;
    PreviewModel *preview() { return &m_preview; }
    int removed() const;
    QVariantList removedKinds() const;
    QStringList customWords() const { return m_custom; }
    QString sizeText() const;
    QString title() const { return m_title; }
    void setTitle(const QString &title);
    QString savedText() const { return m_savedText; }
    QString savedPath() const { return m_savedPath; }
    QString handoff() const { return m_handoff; }
    bool watching() const { return m_watching; }
    QString handoffZip() const { return m_handoffZip; }
    int postedNumber() const { return m_postedNumber; }

    // Step 2 opens: gather for the chosen part and time, unless that is
    // what was last gathered, and remove personal details from it all.
    Q_INVOKABLE void prepare();
    Q_INVOKABLE void setIncluded(const QString &id, bool on);
    Q_INVOKABLE void view(const QString &id);
    Q_INVOKABLE void addWord(const QString &word);
    Q_INVOKABLE void removeWord(const QString &word);
    Q_INVOKABLE void takeScreenshot();
    Q_INVOKABLE void removeScreenshot(int index);
    // Pictures already on the PC: the Open dialog, starting in Pictures >
    // Screenshots, in front of `window`.
    Q_INVOKABLE void chooseScreenshotFiles(QWindow *window);
    void addScreenshotFiles(const QList<QUrl> &files);
    Q_INVOKABLE void suggestTitle();
    Q_INVOKABLE void openGitHub();
    Q_INVOKABLE void reopenGitHub();
    Q_INVOKABLE void endHandoff();
    Q_INVOKABLE void viewPosted();
    Q_INVOKABLE bool saveTo(const QUrl &file);
    Q_INVOKABLE void copyDescription();
    Q_INVOKABLE void copyPreview();
    Q_INVOKABLE void searchIssues();
    Q_INVOKABLE void showSaved();
    Q_INVOKABLE QString suggestedName() const;
    Q_INVOKABLE QUrl downloadsFolder() const;

Q_SIGNALS:
    void partChanged();
    void whenChanged();
    void descriptionChanged();
    void repeatChanged();
    void screenshotsChanged();
    void capturingChanged();
    void crashChanged();
    void busyChanged();
    void itemsChanged();
    void viewingChanged();
    void redactedChanged();
    void sizeChanged();
    void titleChanged();
    void savedChanged();
    void handoffChanged();
    // The window gets out of the way of Capture, and comes back after.
    void hideForCapture();
    void captureDone();
    void message(const QString &text);
    // Another launch asked for the window.
    void raiseRequested();

private Q_SLOTS:
    void pickerResponse(uint response, const QVariantMap &results);
    void pickerOpened();
    void pickerFailed();

private:
    QUrl screenshotsFolder() const;
    // One per process: QML gets it through create(), not a constructor.
    explicit Report(QObject *parent = nullptr);

    struct Section {
        QString id;
        QString file;
        QString raw;
        QString redacted;
        bool included = true;
        int counts[Redactor::CategoryCount] = {};
    };

    QPair<QDateTime, QDateTime> window() const;
    QString describe() const;
    QString repeatText() const;
    // Reads what History handed over, then deletes it; returns the part
    // of Wild OS it is about, or empty.
    QString readEvent(const QString &path);
    void collected(const RawReport &raw);
    void redactAll();
    void measure();
    Section *section(const QString &id);
    const Section *section(const QString &id) const;
    // Static, so the size can be measured off the main thread from a copy.
    static QByteArray zip(const QList<Section> &sections, const QStringList &shots, bool all);
    QString writeZip(const QString &path, bool all);
    QString uniquePath(const QString &folder) const;
    QString issueBody(const QString &zipName, int budget) const;
    void showPreview();
    void setHandoff(const QString &state);
    void watchNext(int seconds);
    void watchOnce();

    QString m_part = QStringLiteral("other");
    QString m_when = QStringLiteral("now");
    int m_pickDay = 0;
    QString m_pickTime;
    QString m_description;
    QString m_repeat;
    QString m_title;
    bool m_titleTouched = false;
    QStringList m_shots;
    // The name a chosen picture had, shown only beside it on this page.
    QHash<QString, QString> m_shotNames;
    QString m_pickerRequest;
    QTemporaryDir m_temp;
    bool m_capturing = false;
    QString m_capturePath;
    QTimer m_capturePoll;
    QDateTime m_captureStarted;
    // The window the gathered report covers, fixed when it is gathered so
    // "Just now" stops moving with the clock; and start-up recovery's own.
    QDateTime m_fixedSince;
    QDateTime m_fixedUntil;
    QString m_crashApp;
    QDateTime m_crashTime;
    // An event handed over from Settings > System > History (--event).
    QString m_eventTitle;
    QDateTime m_eventTime;

    Collector m_collector;
    bool m_collecting = false;
    QString m_collectedKey;
    RawReport m_raw;

    QList<Section> m_sections;
    QStringList m_custom;
    Redactor::Known m_known;
    bool m_redacting = false;
    int m_redactSerial = 0;
    int m_sizeSerial = 0;
    qint64 m_size = -1;
    QString m_viewing = QStringLiteral("logs");
    PreviewModel m_preview;
    QString m_savedText;
    QString m_savedPath;
    QString m_handoff;
    int m_handoffSerial = 0;
    QString m_handoffUrl;
    QString m_handoffZip;
    QDateTime m_handoffAt;
    QTimer m_watchTimer;
    bool m_watching = false;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_watchReply;
    QByteArray m_watchTag;
    int m_postedNumber = 0;
    QString m_postedUrl;
};
