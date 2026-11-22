#include "formatter.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUrl>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

void Formatter::setDocument(QQuickTextDocument *document)
{
    if (document == m_document) {
        return;
    }
    m_document = document;
    Q_EMIT documentChanged();
    connect(this, &Formatter::selectionChanged, this, &Formatter::formatChanged, Qt::UniqueConnection);
}

QTextCursor Formatter::cursor() const
{
    if (!m_document) {
        return {};
    }
    QTextCursor c(m_document->textDocument());
    if (m_start != m_end) {
        c.setPosition(m_start);
        c.setPosition(m_end, QTextCursor::KeepAnchor);
    } else {
        c.setPosition(m_cursor);
    }
    return c;
}

QTextCharFormat Formatter::current() const
{
    QTextCursor c = cursor();
    if (c.isNull()) {
        return {};
    }
    if (!c.hasSelection() && c.position() > 0) {
        // The format of the character before the cursor, which is what
        // the next character typed will take.
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
    }
    return c.charFormat();
}

bool Formatter::bold() const
{
    return current().fontWeight() >= QFont::DemiBold;
}

bool Formatter::italic() const
{
    return current().fontItalic();
}

bool Formatter::underline() const
{
    return current().fontUnderline();
}

void Formatter::merge(const QTextCharFormat &format)
{
    QTextCursor c = cursor();
    if (c.isNull()) {
        return;
    }
    if (!c.hasSelection()) {
        c.select(QTextCursor::WordUnderCursor);
    }
    c.mergeCharFormat(format);
    Q_EMIT formatChanged();
}

void Formatter::toggleBold()
{
    QTextCharFormat f;
    f.setFontWeight(bold() ? QFont::Normal : QFont::Bold);
    merge(f);
}

void Formatter::toggleItalic()
{
    QTextCharFormat f;
    f.setFontItalic(!italic());
    merge(f);
}

void Formatter::toggleUnderline()
{
    QTextCharFormat f;
    f.setFontUnderline(!underline());
    merge(f);
}

void Formatter::setColour(const QColor &colour)
{
    QTextCharFormat f;
    f.setForeground(colour);
    merge(f);
}

void Formatter::setPointSize(int size)
{
    QTextCharFormat f;
    f.setFontPointSize(size);
    merge(f);
}

void Formatter::list(QTextListFormat::Style style)
{
    QTextCursor c = cursor();
    if (c.isNull()) {
        return;
    }
    if (QTextList *existing = c.currentList(); existing && existing->format().style() == style) {
        // Pressing the same list button again takes the list away.
        QTextBlockFormat block = c.blockFormat();
        existing->remove(c.block());
        block.setIndent(0);
        c.setBlockFormat(block);
        return;
    }
    QTextListFormat format;
    format.setStyle(style);
    c.createList(format);
}

void Formatter::bullets()
{
    list(QTextListFormat::ListDisc);
}

void Formatter::numbering()
{
    list(QTextListFormat::ListDecimal);
}

void Formatter::indent(int step)
{
    QTextCursor c = cursor();
    if (c.isNull()) {
        return;
    }
    QTextBlockFormat block = c.blockFormat();
    block.setIndent(qMax(0, block.indent() + step));
    c.setBlockFormat(block);
}

void Formatter::insertLink(const QString &url, const QString &text)
{
    QTextCursor c = cursor();
    if (c.isNull() || url.isEmpty()) {
        return;
    }
    QTextCharFormat f;
    f.setAnchor(true);
    f.setAnchorHref(url);
    f.setFontUnderline(true);
    f.setForeground(QColor(0x1a, 0x5f, 0xd0));
    const QString shown = text.isEmpty() ? (c.hasSelection() ? c.selectedText() : url) : text;
    c.insertText(shown, f);
    c.insertText(QStringLiteral(" "), QTextCharFormat());
}

void Formatter::insertPicture(const QUrl &file)
{
    QTextCursor c = cursor();
    QImage image(file.toLocalFile());
    if (c.isNull() || image.isNull()) {
        return;
    }
    if (image.width() > 640) {
        image = image.scaledToWidth(640, Qt::SmoothTransformation);
    }
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    const int shown = qMin(320, image.width());
    c.insertHtml(QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\">").arg(QString::fromLatin1(data.toBase64())).arg(shown));
}

void Formatter::insertText(const QString &text)
{
    QTextCursor c = cursor();
    if (!c.isNull()) {
        c.insertText(text);
    }
}

QString Formatter::html() const
{
    if (!m_document) {
        return {};
    }
    QString body = m_document->textDocument()->toHtml();
    // Qt writes its own head and a body style naming the app's font; mail
    // gets a plain document in Albert Sans with a system fallback.
    const int start = body.indexOf(QLatin1String("<body"));
    const int open = body.indexOf(QLatin1Char('>'), start);
    const int close = body.lastIndexOf(QLatin1String("</body>"));
    if (start >= 0 && open > start && close > open) {
        body = body.mid(open + 1, close - open - 1);
    }
    // Qt's own style properties mean nothing to other mail programs.
    static const QRegularExpression qtStyle(QStringLiteral(R"(\s*-qt-[a-z-]+:[^;"]*;?)"));
    body.remove(qtStyle);
    return QStringLiteral("<html><head><meta charset=\"utf-8\"></head><body style=\"font-family: 'Albert Sans', 'Segoe UI', Arial, sans-serif; font-size: 11pt;\">")
        + body + QStringLiteral("</body></html>");
}

QString Formatter::plainText() const
{
    return m_document ? m_document->textDocument()->toPlainText() : QString();
}
