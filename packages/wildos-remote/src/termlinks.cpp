#include "termlinks.h"

#include <QBuffer>
#include <QEvent>
#include <QFontMetrics>
#include <QLine>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QSysInfo>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstring>
#include <cwchar>

namespace
{
// How far an address may run: rows, and characters in all.
constexpr int maxRows = 64;
constexpr int maxLength = 4096;
// Reading the rows reads everything the terminal has kept. Past this many
// rows that takes too long to do under the pointer, and addresses are left
// as the emulator finds them.
constexpr int maxKept = 250000;
// What the emulator leaves off the end of an address.
const char trailing[] = "!,.]}";

bool isTrailing(QChar c)
{
    return c.unicode() < 0x7f && std::strchr(trailing, char(c.unicode())) != nullptr;
}

// The characters an address is made of (RFC 3986), less the quote the
// emulator ends an address at.
bool legal(QChar c)
{
    const ushort u = c.unicode();
    if (u <= 0x20 || u > 0x7e) {
        return false;
    }
    if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) {
        return true;
    }
    return std::strchr("-._~:/?#[]@!$&()*+,;=%", char(u)) != nullptr;
}

// The columns a piece of text takes.
int cells(const QString &text)
{
    bool plain = true;
    for (const QChar c : text) {
        if (c.unicode() > 0x7e) {
            plain = false;
            break;
        }
    }
    if (plain) {
        return int(text.size());
    }
    int width = 0;
    const QList<uint> points = text.toUcs4();
    for (const uint c : points) {
        const int w = ::wcwidth(wchar_t(c));
        width += w < 0 ? 1 : w;
    }
    return width;
}

Konsole::UrlFilter::HotSpot *address(Konsole::Filter::HotSpot *spot)
{
    auto *found = dynamic_cast<Konsole::UrlFilter::HotSpot *>(spot);
    return found && !found->capturedTexts().isEmpty() ? found : nullptr;
}
}

// The line under a joined address. The emulator underlines only the rows
// it found itself, and nothing can draw inside its widget, so this lies
// over it and never takes the pointer.
class TermLinkLines : public QWidget
{
public:
    explicit TermLinkLines(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    void set(const QList<QLine> &lines, const QColor &colour)
    {
        if (lines.isEmpty()) {
            m_lines.clear();
            hide();
            return;
        }
        if (lines == m_lines && colour == m_colour && isVisible()) {
            return;
        }
        m_lines = lines;
        m_colour = colour;
        setGeometry(parentWidget()->rect());
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QPen(m_colour));
        p.drawLines(m_lines);
    }

private:
    QList<QLine> m_lines;
    QColor m_colour;
};

TermLinks::TermLinks(QTermWidget *terminal, QWidget *display)
    : QObject(terminal)
    , m_terminal(terminal)
    , m_display(display)
    , m_lines(new TermLinkLines(terminal))
{
    const int method = display->metaObject()->indexOfMethod("hyperlinkAt(QPoint)");
    if (method >= 0) {
        m_marked = display->metaObject()->method(method);
    }
    m_scroll = display->findChild<QScrollBar *>();

    // Text arriving and scrolling move what is under a pointer that stays
    // still; the look for it waits a moment so a busy program costs one
    // look, not one for each write.
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(120);
    connect(&m_refresh, &QTimer::timeout, this, &TermLinks::hover);
    connect(terminal, &QTermWidget::receivedData, this, [this] {
        m_rows.valid = false;
        later();
    });
    if (m_scroll) {
        connect(m_scroll.data(), &QScrollBar::valueChanged, this, &TermLinks::later);
    }

    // The emulator reports a click on an address it found, and on a marked
    // link, while it handles the press this filter has just seen.
    connect(terminal, &QTermWidget::urlActivated, this, [this](const QUrl &url, bool) {
        if (m_tookPress) {
            return;
        }
        const QUrl target = openable(url, m_pressMarked);
        if (target.isValid()) {
            Q_EMIT activated(target);
        }
    });
    display->installEventFilter(this);
}

void TermLinks::setColour(const QColor &colour)
{
    m_colour = colour;
}

QUrl TermLinks::linkAt(const QPoint &position)
{
    if (!m_display) {
        return {};
    }
    m_rows.valid = false;
    return url(find(m_display->mapFrom(m_terminal, position)));
}

bool TermLinks::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_display) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseMove:
        m_pointer = static_cast<QMouseEvent *>(event)->position().toPoint();
        m_inside = true;
        hover();
        break;
    case QEvent::Leave:
        m_inside = false;
        hover();
        break;
    case QEvent::Resize:
        m_rows.valid = false;
        m_lines->set({}, m_colour);
        later();
        break;
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_tookPress = false;
        m_pressMarked = false;
        if (mouse->button() == Qt::LeftButton) {
            m_rows.valid = false;
            const Link link = find(mouse->position().toPoint());
            m_pressMarked = link.marked;
            if (link.joined) {
                const QUrl target = url(link);
                if (target.isValid()) {
                    m_tookPress = true;
                    Q_EMIT activated(target);
                }
            }
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void TermLinks::later()
{
    if ((m_inside || m_lines->isVisible()) && !m_refresh.isActive()) {
        m_refresh.start();
    }
}

void TermLinks::hover()
{
    QList<QLine> lines;
    if (m_inside && m_display) {
        const Link link = find(m_pointer);
        const Grid g = grid();
        if (link.joined && g.valid) {
            // Where the emulator draws the line under an address it found.
            const QFontMetrics metrics(m_display->font());
            const QPoint origin = m_display->mapTo(m_terminal, QPoint(0, 0));
            for (const Span &span : link.spans) {
                if (span.row < 0 || span.row >= g.rows || span.to <= span.from) {
                    continue;
                }
                const int y = (span.row + 1) * g.height - 1 + g.top - metrics.descent() + metrics.underlinePos() + origin.y();
                lines.append(QLine(span.from * g.width + 1 + g.left + origin.x(), y, span.to * g.width - 1 + g.left + origin.x(), y));
            }
        }
    }
    m_lines->set(lines, m_colour);
}

TermLinks::Grid TermLinks::grid() const
{
    Grid g;
    if (!m_display) {
        return g;
    }
    // The emulator says where its cursor is drawn and which column it is
    // in, which gives the size of a character and where column 0 starts.
    // The text starts below the margin the terminal was given.
    const QRect cursor = m_display->inputMethodQuery(Qt::ImCursorRectangle).toRect();
    g.width = cursor.width();
    g.height = cursor.height();
    g.left = cursor.left() - m_display->inputMethodQuery(Qt::ImCursorPosition).toInt() * g.width;
    g.top = m_terminal->getMargin();
    g.columns = m_terminal->screenColumnsCount();
    g.rows = m_terminal->screenLinesCount();
    g.valid = g.width > 0 && g.height > 0 && g.columns > 0 && g.rows > 0;
    return g;
}

TermLinks::Link TermLinks::find(const QPoint &point)
{
    Link link;
    if (!m_display) {
        return link;
    }
    if (m_marked.isValid()) {
        QString target;
        if (m_marked.invoke(m_display.data(), Qt::DirectConnection, Q_RETURN_ARG(QString, target), Q_ARG(QPoint, point)) && !target.isEmpty()) {
            link.text = target;
            link.marked = true;
            return link;
        }
    }

    auto *direct = address(m_terminal->getHotSpotAt(point));
    const Grid g = grid();
    if (g.valid) {
        const int row = std::clamp((point.y() - g.top) / g.height, 0, g.rows - 1);
        const int column = std::clamp((point.x() + g.width / 2 - g.left) / g.width, 0, g.columns);
        // Only while the grid worked out here is the emulator's own: both
        // must find the same thing under the pointer.
        if (address(m_terminal->getHotSpotAt(row, column)) == direct) {
            // A later row of an address that began above. The highest row
            // that reaches this one wins: part of a long address can read
            // as an address of its own (a "www." inside it).
            Link above;
            for (int up = 1; up <= maxRows && up <= row; ++up) {
                Konsole::UrlFilter::HotSpot *head = nullptr;
                for (int tail = 0; tail <= 3 && !head; ++tail) {
                    auto *spot = address(m_terminal->getHotSpotAt(row - up, g.columns - tail));
                    if (spot && spot->endLine() == row - up && spot->endColumn() == g.columns - tail) {
                        head = spot;
                    }
                }
                if (!head) {
                    continue;
                }
                // The same check for a row with nothing under the pointer:
                // the emulator must find this address where the grid puts it.
                const QPoint start(g.left + head->startColumn() * g.width + g.width / 2, g.top + head->startLine() * g.height + g.height / 2);
                if (address(m_terminal->getHotSpotAt(start)) != head) {
                    continue;
                }
                const Link candidate = whole(head, g);
                for (const Span &span : candidate.spans) {
                    if (candidate.joined && span.row == row && column >= span.from && column <= span.to) {
                        above = candidate;
                        break;
                    }
                }
            }
            if (above.joined) {
                return above;
            }
            if (direct) {
                const Link candidate = whole(direct, g);
                if (candidate.joined) {
                    return candidate;
                }
            }
        }
    }
    if (direct) {
        link.text = direct->capturedTexts().constFirst();
    }
    return link;
}

void TermLinks::loadRows()
{
    if (m_rows.valid) {
        return;
    }
    m_rows = Rows();
    m_rows.valid = true;
    const int columns = m_terminal->screenColumnsCount();
    const int kept = m_terminal->historyLinesCount() + m_terminal->screenLinesCount();
    if (columns < 8 || kept <= 0 || kept > maxKept) {
        return;
    }
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    m_terminal->saveHistory(&buffer);
    QStringList lines = QString::fromUtf8(buffer.data()).split(QLatin1Char('\n'));
    // The copy ends with one row past the last and a line break after it.
    if (lines.size() >= 2 && lines.constLast().isEmpty() && lines.at(lines.size() - 2).isEmpty()) {
        lines.removeLast();
        lines.removeLast();
    }
    int row = -1;
    m_rows.last.reserve(lines.size());
    for (const QString &line : std::as_const(lines)) {
        const int width = cells(line);
        row += width <= columns ? 1 : (width + columns - 1) / columns;
        m_rows.last.append(row);
    }
    m_rows.columns = columns;
    m_rows.lines = lines;
    // Spaces at the end of a wrapped row are not in the copy, which can
    // make a line look a row shorter than it is. The count is only trusted
    // when it adds up to the rows the terminal says it holds.
    m_rows.exact = row + 1 == kept;
}

bool TermLinks::headed(int line, const QString &head, int tail) const
{
    const QString &text = m_rows.lines.at(line);
    const qsizetype at = text.size() - tail - head.size();
    if (at < 0 || QStringView(text).mid(at, head.size()) != head) {
        return false;
    }
    for (qsizetype i = text.size() - tail; i < text.size(); ++i) {
        if (!isTrailing(text.at(i))) {
            return false;
        }
    }
    return true;
}

TermLinks::Link TermLinks::extend(int line, const Konsole::UrlFilter::HotSpot *head, int tail, const Grid &grid) const
{
    Link link;
    link.text = head->capturedTexts().constFirst() + m_rows.lines.at(line).right(tail);
    QList<Span> rows;
    int row = head->endLine();
    bool open = false;
    for (int next = line + 1; next < m_rows.lines.size() && rows.size() < maxRows && link.text.size() < maxLength; ++next) {
        const QString &text = m_rows.lines.at(next);
        qsizetype run = 0;
        while (run < text.size() && legal(text.at(run))) {
            ++run;
        }
        // The row has to be the address and nothing else: a word that
        // happens to start the next line is not part of it.
        if (run == 0 || run != text.size() || run > grid.columns) {
            break;
        }
        ++row;
        link.text += text;
        rows.append(Span{row, 0, int(run)});
        open = run == grid.columns;
        if (!open) {
            break;
        }
    }
    if (rows.isEmpty()) {
        link.text = head->capturedTexts().constFirst();
        return link;
    }
    // A last row that ends like a shell prompt (user@host:~$) is one,
    // below an address that happened to end at the edge.
    if (!open && (link.text.endsWith(QLatin1Char('$')) || link.text.endsWith(QLatin1Char('#')))) {
        link.text = head->capturedTexts().constFirst();
        return link;
    }
    while (!rows.isEmpty() && isTrailing(link.text.back())) {
        link.text.chop(1);
        if (--rows.last().to <= 0) {
            rows.removeLast();
        }
    }
    if (rows.isEmpty()) {
        link.text = head->capturedTexts().constFirst();
        return link;
    }

    link.joined = true;
    for (int r = head->startLine(); r <= head->endLine(); ++r) {
        link.spans.append(Span{r, r == head->startLine() ? head->startColumn() : 0, grid.columns});
    }
    link.spans.append(rows);
    return link;
}

TermLinks::Link TermLinks::whole(const Konsole::UrlFilter::HotSpot *head, const Grid &grid)
{
    const QString text = head->capturedTexts().constFirst();
    // The address must run to the end of its row, bar the punctuation the
    // emulator leaves off, and be one that opens in a browser.
    const int tail = grid.columns - head->endColumn();
    if (tail < 0 || tail > 3 || (!text.contains(QLatin1String("://")) && !text.startsWith(QLatin1String("www.")))) {
        return {};
    }
    loadRows();
    if (m_rows.lines.isEmpty() || m_rows.columns != grid.columns) {
        return {};
    }

    if (m_rows.exact) {
        const int row = (m_scroll ? m_scroll->value() : m_terminal->historyLinesCount()) + head->endLine();
        const auto at = std::lower_bound(m_rows.last.cbegin(), m_rows.last.cend(), row);
        if (at == m_rows.last.cend() || *at != row) {
            return {};
        }
        const int line = int(at - m_rows.last.cbegin());
        return headed(line, text, tail) ? extend(line, head, tail, grid) : Link();
    }

    // The rows could not be counted, so the address is looked for by its
    // text. Printed more than once (a sign-in tried twice), it may carry on
    // differently each time, and then there is no telling which this is.
    Link found;
    bool any = false;
    for (int line = 0; line < m_rows.lines.size(); ++line) {
        if (!headed(line, text, tail)) {
            continue;
        }
        const Link one = extend(line, head, tail, grid);
        if (any && one.text != found.text) {
            return {};
        }
        found = one;
        any = true;
    }
    return found;
}

QUrl TermLinks::url(const Link &link) const
{
    if (link.text.isEmpty()) {
        return {};
    }
    if (link.marked) {
        return openable(QUrl(link.text), true);
    }
    if (!link.text.contains(QLatin1String("://")) && link.text.contains(QLatin1Char('@')) && !link.text.startsWith(QLatin1String("www."))) {
        return openable(QUrl(QStringLiteral("mailto:") + link.text), false);
    }
    return openable(QUrl::fromUserInput(link.text), false);
}

QUrl TermLinks::openable(const QUrl &url, bool marked)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        return {};
    }
    const QString scheme = url.scheme();
    if (scheme == QLatin1String("file")) {
        // A listing links each file with the name of the computer it is
        // on. One on another computer (over SSH) is not a file here.
        const QString host = url.host();
        if (!host.isEmpty() && host != QLatin1String("localhost") && host.compare(QSysInfo::machineHostName(), Qt::CaseInsensitive) != 0) {
            return {};
        }
        return QUrl::fromLocalFile(url.path(QUrl::FullyDecoded));
    }
    // A marked link does not show where it goes until the pointer rests on
    // it, so it opens only what a browser, a mail app or Files would.
    if (marked && scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("ftp")
        && scheme != QLatin1String("ftps") && scheme != QLatin1String("mailto")) {
        return {};
    }
    return url;
}
