#include "document.h"

#include "history.h"
#include "signatures.h"

#include <poppler-annotation.h>
#include <poppler-form.h>
#include <poppler-link.h>
#include <poppler-qt6.h>

#include <QClipboard>
#include <QDBusPendingCall>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QMimeData>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QSaveFile>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace
{
// A pen colour that reads as ink on paper and on a printout, the dark blue
// of the board's signature.
const QColor inkColour(0x1f, 0x2a, 0x6b);
const QColor highlightColour(0xff, 0xe5, 0x8a);
// Text added with the Text tool and the signing date. Liberation Sans is
// installed with Wild OS and has Helvetica's widths, so what is typed on
// the page and what Poppler embeds for it line up.
const QString textFamily = QStringLiteral("Liberation Sans");

// The largest page image drawn: past this, zooming further scales the
// image up rather than asking for one too big for the graphics card.
constexpr int maxSide = 8192;
constexpr qint64 maxPixels = 40'000'000;

std::atomic_int findSerial{0};

QVariantMap rectMap(const QRectF &r)
{
    return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
}

QRectF mapRect(const QVariant &v)
{
    const QVariantMap m = v.toMap();
    return QRectF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble(), m.value(QStringLiteral("width")).toDouble(),
                  m.value(QStringLiteral("height")).toDouble());
}

QRectF normalised(const QRectF &points, const QSizeF &page)
{
    if (page.width() <= 0 || page.height() <= 0) {
        return {};
    }
    return QRectF(points.x() / page.width(), points.y() / page.height(), points.width() / page.width(), points.height() / page.height());
}

QRectF clampToPage(const QRectF &r)
{
    return r.intersected(QRectF(0, 0, 1, 1));
}

// The outline as rows, each a level deeper than the one it sits under.
void flatten(const QVector<Poppler::OutlineItem> &items, int level, int pageCount, QVariantList &out)
{
    for (const Poppler::OutlineItem &item : items) {
        if (item.isNull()) {
            continue;
        }
        const QSharedPointer<const Poppler::LinkDestination> to = item.destination();
        const int page = to ? to->pageNumber() - 1 : -1;
        out << QVariantMap{{QStringLiteral("title"), item.name()},
                           {QStringLiteral("level"), level},
                           {QStringLiteral("page"), page >= 0 && page < pageCount ? page : -1},
                           {QStringLiteral("top"), to && to->isChangeTop() ? std::clamp(to->top(), 0.0, 1.0) : 0.0}};
        // Deep enough for any book; a file that nests without end stops here.
        if (level < 12 && item.hasChildren()) {
            flatten(item.children(), level + 1, pageCount, out);
        }
    }
}

double distanceTo(const QRectF &r, const QPointF &p)
{
    const double dx = std::max({r.left() - p.x(), 0.0, p.x() - r.right()});
    const double dy = std::max({r.top() - p.y(), 0.0, p.y() - r.bottom()});
    return std::hypot(dx, dy);
}
}

PdfDocument *PdfDocument::instance()
{
    static PdfDocument *document = new PdfDocument;
    return document;
}

PdfDocument *PdfDocument::create(QQmlEngine *, QJSEngine *)
{
    PdfDocument *document = instance();
    QJSEngine::setObjectOwnership(document, QJSEngine::CppOwnership);
    return document;
}

PdfDocument::PdfDocument(QObject *parent)
    : QObject(parent)
{
}

PdfDocument::~PdfDocument() = default;

bool PdfDocument::loaded() const
{
    QMutexLocker locker(&m_lock);
    return m_doc && !m_doc->isLocked();
}

bool PdfDocument::locked() const
{
    QMutexLocker locker(&m_lock);
    return m_doc && m_doc->isLocked();
}

QString PdfDocument::path() const
{
    return m_path;
}

QString PdfDocument::fileName() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

int PdfDocument::pageCount() const
{
    return m_pageCount;
}

QVariantList PdfDocument::pageSizes() const
{
    return m_pageSizes;
}

int PdfDocument::generation() const
{
    return m_generation;
}

bool PdfDocument::canAnnotate() const
{
    QMutexLocker locker(&m_lock);
    return m_doc && !m_doc->isLocked() && m_doc->okToAddNotes();
}

bool PdfDocument::canFill() const
{
    QMutexLocker locker(&m_lock);
    return m_doc && !m_doc->isLocked() && m_doc->okToFillForm();
}

bool PdfDocument::canPrint() const
{
    QMutexLocker locker(&m_lock);
    return m_doc && !m_doc->isLocked() && m_doc->okToPrint();
}

bool PdfDocument::hasForm() const
{
    QMutexLocker locker(&m_lock);
    return m_doc && !m_doc->isLocked() && m_doc->formType() == Poppler::Document::AcroForm;
}

bool PdfDocument::modified() const
{
    return m_modified;
}

bool PdfDocument::canUndo() const
{
    return !m_added.isEmpty();
}

bool PdfDocument::canRedo() const
{
    return !m_undone.isEmpty();
}

QVariantList PdfDocument::revisions() const
{
    QVariantList out;
    out.reserve(m_revisions.size());
    for (int r : m_revisions) {
        out << r;
    }
    return out;
}

QVariantList PdfDocument::outline() const
{
    return m_outline;
}

QString PdfDocument::lastError() const
{
    return m_error;
}

void PdfDocument::setError(const QString &error)
{
    m_error = error;
}

QString PdfDocument::open(const QUrl &url)
{
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    m_password.clear();
    return load(file);
}

QString PdfDocument::load(const QString &file)
{
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(file);
    if (!doc) {
        return QFileInfo::exists(file) ? tr("This file isn't a PDF that can be read, or it is damaged.") : tr("The file couldn't be found.");
    }
    {
        QMutexLocker locker(&m_lock);
        m_doc = std::move(doc);
        m_path = QFileInfo(file).absoluteFilePath();
        ++m_generation;
        // A protected PDF saved from here is protected the same way; the
        // password it was opened with this time opens what was saved.
        if (m_doc->isLocked() && !m_password.isEmpty()) {
            m_doc->unlock(m_password, m_password);
        }
    }
    findSerial.fetch_add(1);
    History::instance()->opened(m_path);
    afterLoad();
    return locked() ? QStringLiteral("locked") : QString();
}

void PdfDocument::openInNewWindow(const QUrl &url)
{
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {file});
}

bool PdfDocument::unlock(const QString &password)
{
    {
        QMutexLocker locker(&m_lock);
        if (!m_doc || !m_doc->isLocked()) {
            return true;
        }
        const QByteArray bytes = password.toUtf8();
        // Poppler's unlock returns true when it failed.
        if (m_doc->unlock(bytes, bytes)) {
            return false;
        }
        m_password = bytes;
        ++m_generation;
    }
    afterLoad();
    return true;
}

void PdfDocument::afterLoad()
{
    {
        QMutexLocker locker(&m_lock);
        m_pageSizes.clear();
        m_outline.clear();
        m_revisions.clear();
        m_added.clear();
        m_undone.clear();
        m_modified = false;
        m_pageCount = 0;
        if (m_doc && !m_doc->isLocked()) {
            m_doc->setRenderHint(Poppler::Document::Antialiasing);
            m_doc->setRenderHint(Poppler::Document::TextAntialiasing);
            m_doc->setRenderHint(Poppler::Document::TextHinting);
            m_doc->setRenderHint(Poppler::Document::TextSlightHinting);
            m_pageCount = m_doc->numPages();
            for (int i = 0; i < m_pageCount; ++i) {
                std::unique_ptr<Poppler::Page> page = m_doc->page(i);
                const QSizeF size = page ? page->pageSizeF() : QSizeF(595, 842);
                m_pageSizes << QVariant::fromValue(QSizeF(size.width() > 0 ? size.width() : 595, size.height() > 0 ? size.height() : 842));
                m_revisions << 0;
            }
            flatten(m_doc->outline(), 0, m_pageCount, m_outline);
        }
    }
    Q_EMIT loadedChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT revisionsChanged();
}

QImage PdfDocument::render(int generation, int index, int pixelWidth)
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || m_doc->isLocked() || generation != m_generation || index < 0 || index >= m_pageCount || pixelWidth <= 0) {
        return {};
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return {};
    }
    const QSizeF size = page->pageSizeF();
    if (size.width() <= 0 || size.height() <= 0) {
        return {};
    }
    double width = std::min(pixelWidth, maxSide);
    double height = width * size.height() / size.width();
    if (height > maxSide) {
        width *= maxSide / height;
        height = maxSide;
    }
    if (width * height > maxPixels) {
        const double shrink = std::sqrt(maxPixels / (width * height));
        width *= shrink;
        height *= shrink;
    }
    const double dpi = 72.0 * width / size.width();
    return page->renderToImage(dpi, dpi);
}

void PdfDocument::touch(int page)
{
    if (page >= 0 && page < m_revisions.size()) {
        ++m_revisions[page];
    }
    m_modified = true;
    Q_EMIT revisionsChanged();
    Q_EMIT modifiedChanged();
}

QString PdfDocument::nextName()
{
    return QStringLiteral("wildos-pdf-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++m_names);
}

QVariantMap PdfDocument::selectText(int index, const QPointF &from, const QPointF &to)
{
    QMutexLocker locker(&m_lock);
    QVariantMap out;
    if (!m_doc || m_doc->isLocked() || index < 0 || index >= m_pageCount) {
        return out;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return out;
    }
    const QSizeF size = page->pageSizeF();
    const std::vector<std::unique_ptr<Poppler::TextBox>> words = page->textList();
    if (words.empty()) {
        return out;
    }
    QList<QRectF> boxes;
    boxes.reserve(int(words.size()));
    for (const auto &word : words) {
        boxes << normalised(word->boundingBox(), size);
    }
    auto nearest = [&boxes](const QPointF &p) {
        int best = 0;
        double bestDistance = distanceTo(boxes.constFirst(), p);
        for (int i = 1; i < boxes.size(); ++i) {
            const double d = distanceTo(boxes.at(i), p);
            if (d < bestDistance) {
                best = i;
                bestDistance = d;
            }
        }
        return best;
    };
    int first = nearest(from);
    int last = nearest(to);
    if (first > last) {
        std::swap(first, last);
    }

    QString text;
    QVariantList rects;
    QRectF line;
    for (int i = first; i <= last; ++i) {
        const QRectF box = boxes.at(i);
        text += words.at(i)->text();
        // The words of one line are joined into one band, so a highlight
        // is one strip per line rather than a strip per word.
        const bool sameLine = !line.isNull() && box.top() < line.bottom() && box.bottom() > line.top() && box.left() >= line.left();
        if (sameLine) {
            line = line.united(box);
        } else {
            if (!line.isNull()) {
                rects << rectMap(line);
            }
            line = box;
        }
        if (i < last) {
            const QRectF next = boxes.at(i + 1);
            const bool nextOnLine = next.top() < box.bottom() && next.bottom() > box.top() && next.left() >= box.left();
            if (!nextOnLine) {
                text += QLatin1Char('\n');
            } else if (words.at(i)->hasSpaceAfter()) {
                text += QLatin1Char(' ');
            }
        }
    }
    if (!line.isNull()) {
        rects << rectMap(line);
    }
    out.insert(QStringLiteral("text"), text);
    out.insert(QStringLiteral("rects"), rects);
    return out;
}

void PdfDocument::copyText(const QString &text)
{
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
    }
}

bool PdfDocument::addHighlight(int index, const QVariantList &rects)
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || index < 0 || index >= m_pageCount || rects.isEmpty()) {
        return false;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return false;
    }
    QList<Poppler::HighlightAnnotation::Quad> quads;
    QRectF bounds;
    for (const QVariant &v : rects) {
        const QRectF r = clampToPage(mapRect(v));
        if (r.isEmpty()) {
            continue;
        }
        Poppler::HighlightAnnotation::Quad quad;
        // Top left, top right, bottom right, bottom left: the order
        // Poppler turns into the PDF's QuadPoints.
        quad.points[0] = r.topLeft();
        quad.points[1] = r.topRight();
        quad.points[2] = r.bottomRight();
        quad.points[3] = r.bottomLeft();
        quad.capStart = false;
        quad.capEnd = false;
        quad.feather = 0.0;
        quads << quad;
        bounds = bounds.isNull() ? r : bounds.united(r);
    }
    if (quads.isEmpty()) {
        return false;
    }
    auto annotation = std::make_unique<Poppler::HighlightAnnotation>();
    const QString name = nextName();
    annotation->setUniqueName(name);
    annotation->setBoundary(bounds);
    annotation->setHighlightType(Poppler::HighlightAnnotation::Highlight);
    annotation->setHighlightQuads(quads);
    Poppler::Annotation::Style style = annotation->style();
    style.setColor(highlightColour);
    annotation->setStyle(style);
    page->addAnnotation(annotation.get());
    Added added;
    added.page = index;
    added.name = name;
    added.kind = Added::Highlight;
    added.shape = rects;
    record(added);
    locker.unlock();
    touch(index);
    return true;
}

bool PdfDocument::addInk(int index, const QVariantList &points)
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || index < 0 || index >= m_pageCount || points.size() < 2) {
        return false;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return false;
    }
    QVector<QPointF> path;
    path.reserve(points.size());
    QRectF bounds;
    for (const QVariant &v : points) {
        const QPointF p = v.toPointF();
        const QPointF clamped(std::clamp(p.x(), 0.0, 1.0), std::clamp(p.y(), 0.0, 1.0));
        path << clamped;
        bounds = bounds.isNull() ? QRectF(clamped, QSizeF(0.0001, 0.0001)) : bounds.united(QRectF(clamped, QSizeF(0.0001, 0.0001)));
    }
    // Room for the pen's width around the line.
    const QSizeF size = page->pageSizeF();
    const double padX = size.width() > 0 ? 2.0 / size.width() : 0.004;
    const double padY = size.height() > 0 ? 2.0 / size.height() : 0.003;
    bounds = clampToPage(bounds.adjusted(-padX, -padY, padX, padY));

    auto annotation = std::make_unique<Poppler::InkAnnotation>();
    const QString name = nextName();
    annotation->setUniqueName(name);
    annotation->setBoundary(bounds);
    annotation->setInkPaths({path});
    Poppler::Annotation::Style style = annotation->style();
    style.setColor(inkColour);
    style.setWidth(2.0);
    annotation->setStyle(style);
    page->addAnnotation(annotation.get());
    Added added;
    added.page = index;
    added.name = name;
    added.kind = Added::Ink;
    added.shape = points;
    record(added);
    locker.unlock();
    touch(index);
    return true;
}

bool PdfDocument::addText(int index, const QRectF &rect, const QString &text, double pointSize)
{
    if (text.trimmed().isEmpty()) {
        return false;
    }
    QMutexLocker locker(&m_lock);
    if (!m_doc || index < 0 || index >= m_pageCount) {
        return false;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return false;
    }
    auto annotation = std::make_unique<Poppler::TextAnnotation>(Poppler::TextAnnotation::InPlace);
    const QString name = nextName();
    annotation->setUniqueName(name);
    annotation->setBoundary(clampToPage(rect));
    annotation->setContents(text);
    QFont font(textFamily);
    font.setStyleName(QStringLiteral("Regular"));
    font.setPointSizeF(pointSize);
    annotation->setTextFont(font);
    annotation->setTextColor(Qt::black);
    annotation->setInplaceIntent(Poppler::TextAnnotation::TypeWriter);
    // No box and no fill behind the words: typed text, not a sticky note.
    Poppler::Annotation::Style style = annotation->style();
    style.setColor(Qt::transparent);
    style.setWidth(0.0);
    annotation->setStyle(style);
    page->addAnnotation(annotation.get());
    Added added;
    added.page = index;
    added.name = name;
    added.kind = Added::Text;
    added.rect = rect;
    added.text = text;
    added.pointSize = pointSize;
    record(added);
    locker.unlock();
    touch(index);
    return true;
}

void PdfDocument::record(const Added &added)
{
    m_added.append(added);
    if (!m_redoing) {
        m_undone.clear();
    }
}

void PdfDocument::removeAnnotation(const Added &added)
{
    std::unique_ptr<Poppler::Page> page = m_doc->page(added.page);
    if (!page) {
        return;
    }
    std::vector<std::unique_ptr<Poppler::Annotation>> annotations = page->annotations();
    for (auto &annotation : annotations) {
        if (annotation && annotation->uniqueName() == added.name) {
            // Poppler deletes the object it is given.
            page->removeAnnotation(annotation.release());
            return;
        }
    }
}

void PdfDocument::undo()
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || m_added.isEmpty()) {
        return;
    }
    const Added last = m_added.takeLast();
    removeAnnotation(last);
    m_undone.append(last);
    locker.unlock();
    touch(last.page);
}

void PdfDocument::redo()
{
    Added next;
    {
        QMutexLocker locker(&m_lock);
        if (!m_doc || m_undone.isEmpty()) {
            return;
        }
        next = m_undone.takeLast();
    }
    m_redoing = true;
    bool added = false;
    switch (next.kind) {
    case Added::Highlight:
        added = addHighlight(next.page, next.shape);
        break;
    case Added::Ink:
        added = addInk(next.page, next.shape);
        break;
    case Added::Text:
        added = addText(next.page, next.rect, next.text, next.pointSize);
        break;
    case Added::Stamp:
        break;
    }
    m_redoing = false;
    if (!added) {
        // Nothing was put back, so the menu still has to hear that there
        // is one fewer to redo.
        Q_EMIT modifiedChanged();
    }
}

void PdfDocument::find(const QString &text)
{
    const int serial = findSerial.fetch_add(1) + 1;
    const int generation = m_generation;
    if (text.trimmed().isEmpty()) {
        Q_EMIT found(text, {});
        return;
    }
    QThreadPool::globalInstance()->start([this, text, serial, generation]() {
        QVariantList hits;
        for (int i = 0;; ++i) {
            // A page at a time, so drawing pages can go on in between.
            QMutexLocker locker(&m_lock);
            if (findSerial.load() != serial || generation != m_generation || !m_doc || i >= m_pageCount) {
                break;
            }
            std::unique_ptr<Poppler::Page> page = m_doc->page(i);
            if (!page) {
                continue;
            }
            const QSizeF size = page->pageSizeF();
            const QList<QRectF> rects = page->search(text, Poppler::Page::IgnoreCase | Poppler::Page::IgnoreDiacritics);
            for (const QRectF &r : rects) {
                QVariantMap hit = rectMap(normalised(r, size));
                hit.insert(QStringLiteral("page"), i);
                hits << hit;
            }
        }
        if (findSerial.load() != serial) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, text, hits, serial]() {
            if (findSerial.load() == serial) {
                Q_EMIT found(text, hits);
            }
        }, Qt::QueuedConnection);
    });
}

void PdfDocument::findLinks(int index)
{
    const int generation = m_generation;
    QThreadPool::globalInstance()->start([this, index, generation]() {
        QVariantList links;
        {
            // Off the main thread, so a page being drawn never holds up
            // the window while this waits its turn.
            QMutexLocker locker(&m_lock);
            if (generation != m_generation || !m_doc || m_doc->isLocked() || index < 0 || index >= m_pageCount) {
                return;
            }
            std::unique_ptr<Poppler::Page> page = m_doc->page(index);
            if (!page) {
                return;
            }
            for (const auto &link : page->links()) {
                if (!link) {
                    continue;
                }
                const QRectF area = clampToPage(link->linkArea().normalized());
                if (area.isEmpty()) {
                    continue;
                }
                QVariantMap item = rectMap(area);
                if (link->linkType() == Poppler::Link::Goto) {
                    const auto *jump = static_cast<const Poppler::LinkGoto *>(link.get());
                    const Poppler::LinkDestination to = jump->destination();
                    if (jump->isExternal() || to.pageNumber() < 1 || to.pageNumber() > m_pageCount) {
                        continue;
                    }
                    item.insert(QStringLiteral("page"), to.pageNumber() - 1);
                    item.insert(QStringLiteral("top"), to.isChangeTop() ? std::clamp(to.top(), 0.0, 1.0) : 0.0);
                    item.insert(QStringLiteral("url"), QString());
                } else if (link->linkType() == Poppler::Link::Browse) {
                    const QString address = static_cast<const Poppler::LinkBrowse *>(link.get())->url();
                    // Web pages and mail only: a PDF doesn't get to open
                    // files or start programs through a link.
                    const QString scheme = QUrl(address).scheme().toLower();
                    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("mailto")) {
                        continue;
                    }
                    item.insert(QStringLiteral("page"), -1);
                    item.insert(QStringLiteral("top"), 0.0);
                    item.insert(QStringLiteral("url"), address);
                } else {
                    continue;
                }
                links << item;
            }
        }
        QMetaObject::invokeMethod(this, [this, index, links, generation]() {
            if (generation == m_generation) {
                Q_EMIT linksFound(index, links);
            }
        }, Qt::QueuedConnection);
    });
}

QVariantList PdfDocument::formFields(int index)
{
    QMutexLocker locker(&m_lock);
    QVariantList out;
    if (!m_doc || m_doc->isLocked() || index < 0 || index >= m_pageCount) {
        return out;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return out;
    }
    for (const auto &field : page->formFields()) {
        if (!field || !field->isVisible() || field->isReadOnly()) {
            continue;
        }
        QVariantMap item = rectMap(field->rect());
        item.insert(QStringLiteral("id"), field->id());
        item.insert(QStringLiteral("name"), field->uiName().isEmpty() ? field->name() : field->uiName());
        switch (field->type()) {
        case Poppler::FormField::FormText: {
            auto *text = static_cast<Poppler::FormFieldText *>(field.get());
            if (text->textType() == Poppler::FormFieldText::FileSelect || text->isPassword()) {
                continue;
            }
            item.insert(QStringLiteral("kind"), QStringLiteral("text"));
            item.insert(QStringLiteral("value"), text->text());
            item.insert(QStringLiteral("multiline"), text->textType() == Poppler::FormFieldText::Multiline);
            item.insert(QStringLiteral("fontSize"), text->getFontSize());
            item.insert(QStringLiteral("maxLength"), text->maximumLength());
            break;
        }
        case Poppler::FormField::FormButton: {
            auto *button = static_cast<Poppler::FormFieldButton *>(field.get());
            if (button->buttonType() == Poppler::FormFieldButton::Push) {
                continue;
            }
            item.insert(QStringLiteral("kind"), button->buttonType() == Poppler::FormFieldButton::Radio ? QStringLiteral("radio") : QStringLiteral("check"));
            item.insert(QStringLiteral("checked"), button->state());
            break;
        }
        case Poppler::FormField::FormChoice: {
            auto *choice = static_cast<Poppler::FormFieldChoice *>(field.get());
            const QList<int> current = choice->currentChoices();
            item.insert(QStringLiteral("kind"), QStringLiteral("choice"));
            item.insert(QStringLiteral("choices"), choice->choices());
            item.insert(QStringLiteral("current"), current.isEmpty() ? -1 : current.constFirst());
            break;
        }
        default:
            continue;
        }
        out << item;
    }
    return out;
}

void PdfDocument::setFieldText(int index, int id, const QString &value)
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || index < 0 || index >= m_pageCount) {
        return;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return;
    }
    for (const auto &field : page->formFields()) {
        if (field->id() == id && field->type() == Poppler::FormField::FormText) {
            auto *text = static_cast<Poppler::FormFieldText *>(field.get());
            if (text->text() == value) {
                return;
            }
            text->setText(value);
            locker.unlock();
            touch(index);
            return;
        }
    }
}

void PdfDocument::setFieldChecked(int index, int id, bool checked)
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || index < 0 || index >= m_pageCount) {
        return;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return;
    }
    for (const auto &field : page->formFields()) {
        if (field->id() == id && field->type() == Poppler::FormField::FormButton) {
            static_cast<Poppler::FormFieldButton *>(field.get())->setState(checked);
            locker.unlock();
            touch(index);
            return;
        }
    }
}

void PdfDocument::setFieldChoice(int index, int id, int choiceIndex)
{
    QMutexLocker locker(&m_lock);
    if (!m_doc || index < 0 || index >= m_pageCount) {
        return;
    }
    std::unique_ptr<Poppler::Page> page = m_doc->page(index);
    if (!page) {
        return;
    }
    for (const auto &field : page->formFields()) {
        if (field->id() == id && field->type() == Poppler::FormField::FormChoice) {
            static_cast<Poppler::FormFieldChoice *>(field.get())->setCurrentChoices({choiceIndex});
            locker.unlock();
            touch(index);
            return;
        }
    }
}

bool PdfDocument::writeTo(const QString &target)
{
    // Called with m_lock held. QSaveFile writes beside the target and
    // renames over it only once everything is written, so a failed save
    // leaves the old file whole; Poppler keeps reading the file it opened.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("PDF can't write to that folder. Try saving it somewhere else, such as Documents."));
        return false;
    }
    std::unique_ptr<Poppler::PDFConverter> converter = m_doc->pdfConverter();
    converter->setOutputDevice(&file);
    converter->setPDFOptions(converter->pdfOptions() | Poppler::PDFConverter::WithChanges);
    if (!converter->convert()) {
        file.cancelWriting();
        setError(tr("The PDF couldn't be written."));
        return false;
    }
    if (!file.commit()) {
        setError(tr("The PDF couldn't be written. The disk may be full."));
        return false;
    }
    return true;
}

bool PdfDocument::save()
{
    if (m_path.isEmpty()) {
        return false;
    }
    return saveAs(QUrl::fromLocalFile(m_path));
}

bool PdfDocument::saveAs(const QUrl &url)
{
    const QString target = url.toLocalFile();
    {
        QMutexLocker locker(&m_lock);
        if (!m_doc || target.isEmpty()) {
            return false;
        }
        if (!writeTo(target)) {
            return false;
        }
    }
    // Reopened from what was written, so the next save starts from it and
    // the window shows exactly what is on disk.
    const QString error = load(target);
    if (!error.isEmpty() && error != QLatin1String("locked")) {
        setError(error);
        return false;
    }
    return true;
}

QUrl PdfDocument::signedCopyUrl() const
{
    if (m_path.isEmpty()) {
        return {};
    }
    const QFileInfo info(m_path);
    const QString base = info.completeBaseName();
    QString candidate = info.absolutePath() + QLatin1Char('/') + tr("%1 (signed)").arg(base) + QStringLiteral(".pdf");
    for (int n = 2; QFileInfo::exists(candidate) && n < 100; ++n) {
        candidate = info.absolutePath() + QLatin1Char('/') + tr("%1 (signed %2)").arg(base).arg(n) + QStringLiteral(".pdf");
    }
    return QUrl::fromLocalFile(candidate);
}

QUrl PdfDocument::fileUrl() const
{
    return m_path.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_path);
}

QUrl PdfDocument::folderUrl() const
{
    return m_path.isEmpty() ? QUrl::fromLocalFile(QDir::homePath()) : QUrl::fromLocalFile(QFileInfo(m_path).absolutePath());
}

bool PdfDocument::saveSignedCopy(const QUrl &url, const QVariantList &placements)
{
    const QString target = url.toLocalFile();
    if (target.isEmpty() || placements.isEmpty()) {
        return false;
    }
    if (QFileInfo(target).absoluteFilePath() == m_path) {
        setError(tr("Choose a new name: the signed copy never replaces the original."));
        return false;
    }
    Signatures *signatures = Signatures::instance();
    {
        QMutexLocker locker(&m_lock);
        if (!m_doc) {
            return false;
        }
        QList<Added> stamped;
        for (const QVariant &v : placements) {
            const QVariantMap p = v.toMap();
            const int index = p.value(QStringLiteral("page")).toInt();
            const QRectF rect = clampToPage(mapRect(p));
            if (index < 0 || index >= m_pageCount || rect.isEmpty()) {
                continue;
            }
            std::unique_ptr<Poppler::Page> page = m_doc->page(index);
            if (!page) {
                continue;
            }
            const QString kind = p.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("date")) {
                auto annotation = std::make_unique<Poppler::TextAnnotation>(Poppler::TextAnnotation::InPlace);
                const QString name = nextName();
                annotation->setUniqueName(name);
                annotation->setBoundary(rect);
                annotation->setContents(p.value(QStringLiteral("text")).toString());
                QFont font(textFamily);
                font.setStyleName(QStringLiteral("Regular"));
                font.setPointSizeF(p.value(QStringLiteral("pointSize"), 10.5).toDouble());
                annotation->setTextFont(font);
                annotation->setTextColor(Qt::black);
                annotation->setInplaceIntent(Poppler::TextAnnotation::TypeWriter);
                Poppler::Annotation::Style style = annotation->style();
                style.setColor(Qt::transparent);
                style.setWidth(0.0);
                annotation->setStyle(style);
                page->addAnnotation(annotation.get());
                stamped.append(Added{index, name});
                continue;
            }
            const QImage image = kind == QLatin1String("initials") ? signatures->initialsImage() : signatures->image(p.value(QStringLiteral("signature")).toString());
            if (image.isNull()) {
                continue;
            }
            // A stamp whose appearance is the signature picture itself, kept
            // with its transparency, so it sits on the line like ink.
            auto annotation = std::make_unique<Poppler::StampAnnotation>();
            const QString name = nextName();
            annotation->setUniqueName(name);
            annotation->setBoundary(rect);
            // Poppler stores the picture uncompressed, so it goes in at the
            // size it is placed at, 150 dpi, rather than the size it was drawn.
            const int wide = std::max(1, int(std::ceil(rect.width() * page->pageSizeF().width() / 72.0 * 150.0)));
            const QImage placed = image.width() > wide ? image.scaledToWidth(wide, Qt::SmoothTransformation) : image;
            annotation->setStampCustomImage(placed.convertToFormat(QImage::Format_ARGB32));
            page->addAnnotation(annotation.get());
            stamped.append(Added{index, name});
        }
        if (!writeTo(target)) {
            // Taken off again, so a later Save can't put them in the
            // original.
            for (const Added &added : std::as_const(stamped)) {
                removeAnnotation(added);
            }
            return false;
        }
    }
    // The window moves to the signed copy; the original was only read.
    const QString error = load(target);
    if (!error.isEmpty() && error != QLatin1String("locked")) {
        setError(error);
        return false;
    }
    return true;
}

bool PdfDocument::print()
{
    if (!canPrint()) {
        return false;
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(fileName());
    printer.setFromTo(1, m_pageCount);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(tr("Print %1").arg(fileName()));
    dialog.setMinMax(1, m_pageCount);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    int first = 1;
    int last = m_pageCount;
    if (printer.printRange() == QPrinter::PageRange && printer.fromPage() > 0) {
        first = printer.fromPage();
        last = std::clamp(printer.toPage(), first, m_pageCount);
    }
    QPainter painter;
    if (!painter.begin(&printer)) {
        setError(tr("The printer couldn't be reached."));
        return false;
    }
    // Pages go to the printer as pictures drawn at up to 300 dots per inch,
    // fitted to the paper without changing their shape.
    const int dpi = std::min(printer.resolution(), 300);
    for (int i = first; i <= last; ++i) {
        const QRect paper = painter.viewport();
        const QSizeF size = m_pageSizes.value(i - 1).toSizeF();
        const int pixelWidth = int(std::ceil(size.width() / 72.0 * dpi));
        const QImage image = render(m_generation, i - 1, pixelWidth);
        if (!image.isNull()) {
            const QSize fitted = image.size().scaled(paper.size(), Qt::KeepAspectRatio);
            const QRect target(paper.x() + (paper.width() - fitted.width()) / 2, paper.y() + (paper.height() - fitted.height()) / 2, fitted.width(),
                               fitted.height());
            painter.drawImage(target, image);
        }
        if (i < last) {
            printer.newPage();
        }
    }
    painter.end();
    return true;
}

void PdfDocument::showInFolder()
{
    if (m_path.isEmpty()) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                                                          QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(m_path).toString()} << QString();
    QDBusConnection::sessionBus().asyncCall(message);
}

void PdfDocument::copyFile()
{
    if (m_path.isEmpty()) {
        return;
    }
    auto *data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(m_path)});
    QGuiApplication::clipboard()->setMimeData(data);
}
