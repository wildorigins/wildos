#pragma once

#include <QImage>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QRectF>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "recorder.h"
#include "scanner.h"
#include "sharer.h"

class FrozenImages;
class Grabber;
class KNotification;

// Wild OS Capture (AppCapture.dc), on Print Screen and Win+Shift+S: the
// screen freezes under a dimmed layer with the Capture toolbar (Screenshot
// or Record; area, window, full screen or freeform; a delay). A screenshot
// goes to the clipboard and to Pictures/Screenshots, and opens in the
// editor (AppCaptureEdit.dc) to mark up.
//
// One process at a time: pressing the shortcut again reaches the running
// one through D-Bus. It stays while an editor window, a recording or one of
// its notifications is still about, so their buttons keep working.
class CaptureApp : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool selecting READ selecting NOTIFY selectingChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool recordMode READ recordMode WRITE setRecordMode NOTIFY modeChanged)
    Q_PROPERTY(int delay READ delay WRITE setDelay NOTIFY delayChanged)
    Q_PROPERTY(int countdown READ countdown NOTIFY countdownChanged)
    Q_PROPERTY(int serial READ serial NOTIFY selectingChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(bool canRecord READ canRecord CONSTANT)
    Q_PROPERTY(Recorder *recorder READ recorder CONSTANT)
    Q_PROPERTY(Sharer *sharer READ sharer CONSTANT)
    Q_PROPERTY(Scanner *scanner READ scanner CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    // The toolbar goes on the main screen only.
    Q_PROPERTY(QString primaryScreen READ primaryScreen NOTIFY selectingChanged)
    // The area or window picked so far, which Capture or Enter takes. An
    // area is in its screen's own logical coordinates.
    Q_PROPERTY(QString pickedScreen READ pickedScreen NOTIFY pickChanged)
    Q_PROPERTY(QRectF pickedArea READ pickedArea NOTIFY pickChanged)
    Q_PROPERTY(QVariantMap pickedWindow READ pickedWindow NOTIFY pickChanged)
    // The toolbar's Options menu.
    Q_PROPERTY(bool openEditor READ openEditor WRITE setOpenEditor NOTIFY optionsChanged)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY optionsChanged)
    Q_PROPERTY(bool includePointer READ includePointer WRITE setIncludePointer NOTIFY optionsChanged)

public:
    explicit CaptureApp(QObject *parent = nullptr);
    ~CaptureApp() override;

    FrozenImages *frozen() const { return m_frozen; }

    bool selecting() const { return m_selecting; }
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    bool recordMode() const { return m_recordMode; }
    void setRecordMode(bool on);
    int delay() const { return m_delay; }
    void setDelay(int seconds);
    int countdown() const { return m_countdown; }
    int serial() const { return m_serial; }
    QVariantList windows() const { return m_windows; }
    bool canRecord() const;
    Recorder *recorder() const { return m_recorder; }
    Sharer *sharer() const { return m_sharer; }
    Scanner *scanner() const { return m_scanner; }
    QString message() const { return m_message; }
    QString primaryScreen() const;
    QString pickedScreen() const { return m_pickedScreen; }
    QRectF pickedArea() const { return m_pickedArea; }
    QVariantMap pickedWindow() const { return m_pickedWindow; }
    bool openEditor() const { return m_openEditor; }
    void setOpenEditor(bool on);
    bool autoSave() const { return m_autoSave; }
    void setAutoSave(bool on);
    bool includePointer() const { return m_includePointer; }
    void setIncludePointer(bool on);

    // From the command line, or a second launch: --record, --scan,
    // --mode <name>, --edit <file>.
    void handle(const QStringList &arguments);

    // The overlay's choices, in each screen's own logical coordinates.
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void pickArea(const QString &screen, const QRectF &area);
    Q_INVOKABLE void pickWindow(const QVariantMap &window);
    Q_INVOKABLE void capture(const QString &screen);
    Q_INVOKABLE void takeArea(const QString &screen, const QRectF &area);
    Q_INVOKABLE void takeFreehand(const QString &screen, const QVariantList &points);
    Q_INVOKABLE void takeWindow(const QString &id, const QRectF &globalArea);
    Q_INVOKABLE void takeScreen(const QString &screen);

    // The editor's results. `crop` is in the picture's pixels; an empty one
    // keeps all of it.
    Q_INVOKABLE bool copyImage(const QVariant &image, const QRectF &crop);
    Q_INVOKABLE bool saveImage(const QVariant &image, const QRectF &crop, const QString &path);
    Q_INVOKABLE void openEditorFor(const QUrl &file);
    Q_INVOKABLE void openShare(const QUrl &file);
    Q_INVOKABLE void openFile(const QUrl &file);
    Q_INVOKABLE void showInFolder(const QUrl &file);
    Q_INVOKABLE void editorClosed(const QString &key);
    // The Scan tab: opened from the toolbar's Scan or --scan, and its
    // Screenshot and Record going back to the toolbar.
    Q_INVOKABLE void openScan();
    Q_INVOKABLE void scanClosed();
    Q_INVOKABLE void startCapture(bool record);
    Q_INVOKABLE void markUpScanPage(const QString &path);
    Q_INVOKABLE void windowClosed();

Q_SIGNALS:
    void selectingChanged();
    void modeChanged();
    void delayChanged();
    void countdownChanged();
    void windowsChanged();
    void messageChanged();
    void pickChanged();
    void optionsChanged();
    // `source` is what the editor shows; `path` is where Save writes, and
    // `saved` whether the picture is there already.
    void editRequested(const QUrl &source, const QString &key, const QString &path, bool saved);
    void shareRequested(const QUrl &file);
    void scanRequested();
    // A scanned page in the editor; saving it there sends it back.
    void scanPageEditRequested(const QUrl &source, const QString &path);

private:
    void freeze();
    void clearPick();
    void after(std::function<void()> action);
    void finish(const QImage &image);
    void recordingFinished(const QString &path);
    void notifyFailure(const QString &title, const QString &text);
    void setMessage(const QString &message);
    QString nextPath() const;
    void quitWhenIdle();
    QImage crop(const QString &screen, const QRectF &area) const;

    FrozenImages *m_frozen;
    Grabber *m_grabber;
    Recorder *m_recorder;
    Sharer *m_sharer;
    Scanner *m_scanner;
    bool m_scanOpen = false;
    // The toolbar came from the Scan window's Screenshot or Record.
    bool m_fromScan = false;
    bool m_selecting = false;
    QString m_mode = QStringLiteral("rectangle");
    bool m_recordMode = false;
    int m_delay = 0;
    int m_countdown = 0;
    int m_serial = 0;
    int m_openWindows = 0;
    bool m_openEditor = true;
    bool m_autoSave = true;
    bool m_includePointer = false;
    QString m_pickedScreen;
    QRectF m_pickedArea;
    QVariantMap m_pickedWindow;
    QVariantList m_windows;
    std::function<void()> m_pending;
    QList<QPointer<KNotification>> m_notifications;
    QTimer m_countdownTimer;
    QTimer m_idleTimer;
    QString m_message;
    // Set by --to: the one screenshot goes to this file, not the clipboard,
    // Pictures or the editor.
    QString m_returnPath;
};
