#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQmlEngine>
#include <QRectF>
#include <QUrl>
#include <QVariant>

#include <memory>

namespace Poppler
{
class Document;
}

// The open PDF, on Poppler. One document per window and one window per
// process: opening a PDF from Files starts a PDF of its own, as Write does.
//
// Every coordinate handed to and from QML is normalised to the page, (0, 0)
// at its top left and (1, 1) at its bottom right, as Poppler's annotations
// and form fields use them, so the page can be drawn at any zoom.
//
// Pages are drawn on worker threads (PageProvider) while QML adds notes and
// fills in fields on the main thread; Poppler's document is not safe to use
// from two threads at once, so every use of it holds m_lock.
class PdfDocument : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY loadedChanged)
    Q_PROPERTY(QString path READ path NOTIFY loadedChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loadedChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY loadedChanged)
    Q_PROPERTY(QVariantList pageSizes READ pageSizes NOTIFY loadedChanged)
    Q_PROPERTY(int generation READ generation NOTIFY loadedChanged)
    Q_PROPERTY(bool canAnnotate READ canAnnotate NOTIFY loadedChanged)
    Q_PROPERTY(bool canFill READ canFill NOTIFY loadedChanged)
    Q_PROPERTY(bool canPrint READ canPrint NOTIFY loadedChanged)
    Q_PROPERTY(bool hasForm READ hasForm NOTIFY loadedChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY modifiedChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY modifiedChanged)
    Q_PROPERTY(QVariantList revisions READ revisions NOTIFY revisionsChanged)
    Q_PROPERTY(QVariantList outline READ outline NOTIFY loadedChanged)

public:
    static PdfDocument *instance();
    static PdfDocument *create(QQmlEngine *, QJSEngine *);
    ~PdfDocument() override;

    bool loaded() const;
    bool locked() const;
    QString path() const;
    QString fileName() const;
    int pageCount() const;
    QVariantList pageSizes() const;
    int generation() const;
    bool canAnnotate() const;
    bool canFill() const;
    bool canPrint() const;
    bool hasForm() const;
    bool modified() const;
    bool canUndo() const;
    bool canRedo() const;
    QVariantList revisions() const;
    // The PDF's own contents, if it has one, as one flat list: { title,
    // level (0 at the top), page (-1 when it leads nowhere in this file),
    // top (how far down that page, 0 to 1) }.
    QVariantList outline() const;

    // For PageProvider, on a worker thread: the page drawn pixelWidth wide,
    // or a null image if the document has changed since it was asked for.
    QImage render(int generation, int page, int pixelWidth);

    // "" when it opened, "locked" when it needs a password, otherwise a
    // sentence saying why it could not be opened.
    Q_INVOKABLE QString open(const QUrl &url);
    Q_INVOKABLE bool unlock(const QString &password);
    Q_INVOKABLE void openInNewWindow(const QUrl &url);

    // From one point to another across the words on a page:
    // { text, rects: [{ x, y, width, height }] }.
    Q_INVOKABLE QVariantMap selectText(int page, const QPointF &from, const QPointF &to);
    Q_INVOKABLE void copyText(const QString &text);

    Q_INVOKABLE bool addHighlight(int page, const QVariantList &rects);
    Q_INVOKABLE bool addInk(int page, const QVariantList &points);
    Q_INVOKABLE bool addText(int page, const QRectF &rect, const QString &text, double pointSize);
    Q_INVOKABLE void undo();
    // Puts back what Undo took off. Adding anything new empties it.
    Q_INVOKABLE void redo();

    // Runs on a worker thread and answers with found().
    Q_INVOKABLE void find(const QString &text);
    // The links on a page, read on a worker thread and answered with
    // linksFound(): { x, y, width, height, page, top, url }. A link inside
    // the file has its page and how far down it; a web or mail link has
    // page -1 and its address. Other kinds are left out.
    Q_INVOKABLE void findLinks(int page);

    // The fields of a page that can be filled in: { id, kind (text, check,
    // radio, choice), x, y, width, height, value, checked, choices,
    // current, multiline, fontSize, name }.
    Q_INVOKABLE QVariantList formFields(int page);
    Q_INVOKABLE void setFieldText(int page, int id, const QString &text);
    Q_INVOKABLE void setFieldChecked(int page, int id, bool checked);
    Q_INVOKABLE void setFieldChoice(int page, int id, int index);

    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &url);
    // Places each signature, date and set of initials ({ page, x, y, width,
    // height, kind: "signature" | "initials" | "date", signature, text })
    // and writes the result to a new file, then opens that file. The file
    // this was opened from is never written.
    Q_INVOKABLE bool saveSignedCopy(const QUrl &url, const QVariantList &placements);
    Q_INVOKABLE QUrl signedCopyUrl() const;
    Q_INVOKABLE QUrl fileUrl() const;
    Q_INVOKABLE QUrl folderUrl() const;

    Q_INVOKABLE bool print();
    Q_INVOKABLE void showInFolder();
    Q_INVOKABLE void copyFile();
    Q_INVOKABLE QString lastError() const;

Q_SIGNALS:
    void loadedChanged();
    void modifiedChanged();
    void revisionsChanged();
    void found(const QString &text, const QVariantList &hits);
    void linksFound(int page, const QVariantList &links);

private:
    explicit PdfDocument(QObject *parent = nullptr);

    // What was added, with what it takes to add it again after an Undo.
    struct Added {
        enum Kind { Highlight, Ink, Text, Stamp };
        int page = 0;
        QString name;
        Kind kind = Stamp;
        QVariantList shape;
        QRectF rect;
        QString text;
        double pointSize = 0;
    };

    QString load(const QString &path);
    void afterLoad();
    void touch(int page);
    // Called with m_lock held.
    void removeAnnotation(const Added &added);
    // Called with m_lock held.
    void record(const Added &added);
    QString nextName();
    bool writeTo(const QString &path);
    void setError(const QString &error);

    mutable QMutex m_lock;
    std::unique_ptr<Poppler::Document> m_doc;
    QString m_path;
    QByteArray m_password;
    int m_generation = 0;
    int m_pageCount = 0;
    QVariantList m_pageSizes;
    QVariantList m_outline;
    QList<int> m_revisions;
    QList<Added> m_added;
    QList<Added> m_undone;
    bool m_redoing = false;
    bool m_modified = false;
    int m_names = 0;
    QString m_error;
};
