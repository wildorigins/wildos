#include "htmlview.h"

#include <QCursor>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPrintDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QUrl>

#include <utility>

#include "messageview.h"

namespace
{
QColor colourOf(const litehtml::web_color &c)
{
    return QColor(c.red, c.green, c.blue, c.alpha);
}

QString utf8(const litehtml::tchar_t *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

const QByteArray &masterCss()
{
    static const QByteArray css = []() {
        QFile file(QStringLiteral(":/qt/qml/WildOS/Mail/data/litehtml-master.css"));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    return css;
}

QPainterPath roundedPath(const QRectF &r, const litehtml::border_radiuses &rad)
{
    QPainterPath path;
    const qreal tl = rad.top_left_x;
    const qreal tr = rad.top_right_x;
    const qreal br = rad.bottom_right_x;
    const qreal bl = rad.bottom_left_x;
    path.moveTo(r.left() + tl, r.top());
    path.lineTo(r.right() - tr, r.top());
    if (tr > 0) {
        path.arcTo(QRectF(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr), 90, -90);
    }
    path.lineTo(r.right(), r.bottom() - br);
    if (br > 0) {
        path.arcTo(QRectF(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
    }
    path.lineTo(r.left() + bl, r.bottom());
    if (bl > 0) {
        path.arcTo(QRectF(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl), 270, -90);
    }
    path.lineTo(r.left(), r.top() + tl);
    if (tl > 0) {
        path.arcTo(QRectF(r.left(), r.top(), 2 * tl, 2 * tl), 180, -90);
    }
    path.closeSubpath();
    return path;
}

bool hasRadius(const litehtml::border_radiuses &r)
{
    return r.top_left_x || r.top_right_x || r.bottom_left_x || r.bottom_right_x;
}
}

HtmlView::HtmlView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAntialiasing(true);
    setOpaquePainting(false);
}

HtmlView::~HtmlView()
{
    m_document.reset();
}

void HtmlView::setHtml(const QString &html)
{
    if (html == m_html) {
        return;
    }
    m_html = html;
    m_images.clear();
    m_fetching.clear();
    Q_EMIT htmlChanged();
    rebuild();
}

void HtmlView::setMessage(MessageView *message)
{
    if (message != m_message) {
        m_message = message;
        Q_EMIT messageChanged();
    }
}

void HtmlView::setAllowRemote(bool allow)
{
    if (allow == m_allowRemote) {
        return;
    }
    m_allowRemote = allow;
    Q_EMIT allowRemoteChanged();
    rebuild();
}

void HtmlView::setPaper(bool paper)
{
    if (paper != m_paper) {
        m_paper = paper;
        Q_EMIT paperChanged();
        rebuild();
    }
}

void HtmlView::setTextColor(const QColor &c)
{
    if (c != m_textColor) {
        m_textColor = c;
        Q_EMIT coloursChanged();
        rebuild();
    }
}

void HtmlView::setLinkColor(const QColor &c)
{
    if (c != m_linkColor) {
        m_linkColor = c;
        Q_EMIT coloursChanged();
        rebuild();
    }
}

void HtmlView::setQuoteColor(const QColor &c)
{
    if (c != m_quoteColor) {
        m_quoteColor = c;
        Q_EMIT coloursChanged();
        rebuild();
    }
}

void HtmlView::setFontSize(qreal size)
{
    if (!qFuzzyCompare(size, m_fontSize)) {
        m_fontSize = size;
        Q_EMIT coloursChanged();
        rebuild();
    }
}

void HtmlView::setContentY(qreal y)
{
    if (!qFuzzyCompare(y + 1, m_contentY + 1)) {
        m_contentY = y;
        Q_EMIT contentYChanged();
        update();
    }
}

void HtmlView::rebuild()
{
    m_document.reset();
    m_fonts.clear();
    if (m_html.isEmpty()) {
        m_contentHeight = 0;
        Q_EMIT contentHeightChanged();
        update();
        return;
    }
    if (!m_context) {
        m_context = std::make_unique<litehtml::context>();
        m_context->load_master_stylesheet(masterCss().constData());
    }
    // How Mail dresses a message: plain mail in the window's own type and
    // colours; designed mail on white with dark text, as its sender saw it.
    const QString ink = m_paper ? QStringLiteral("#1f2125") : m_textColor.name(QColor::HexRgb);
    const QString link = m_paper ? QStringLiteral("#1a5fd0") : m_linkColor.name(QColor::HexRgb);
    const QString quote = m_paper ? QStringLiteral("#5b6275") : m_quoteColor.name(QColor::HexRgb);
    const QString css = QStringLiteral(
                            "html, body { height: auto; }"
                            "body { color: %1; font-family: 'Albert Sans', 'Segoe UI', Arial, sans-serif; font-size: %4px; line-height: 1.6; margin: 0; }"
                            "a { color: %2; }"
                            "blockquote { margin: 0 0 0 4px; padding-left: 12px; border-left: 2px solid %3; color: %3; }"
                            "img { max-width: 100%; }"
                            "p { margin: 0 0 15px 0; }")
                            .arg(ink, link, quote)
                            .arg(m_fontSize);
    const QByteArray userCss = css.toUtf8();
    litehtml::css user;
    user.parse_stylesheet(userCss.constData(), nullptr, nullptr, litehtml::media_query_list::ptr());
    user.sort_selectors();
    m_pageColour = QColor();
    static const QRegularExpression bodyTag(QStringLiteral(R"(<body\b[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bgAttr(QStringLiteral(R"(\bbgcolor\s*=\s*["']?(#?[0-9a-z]+))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bgStyle(QStringLiteral(R"(background(?:-color)?\s*:\s*(#[0-9a-f]{3,8}|rgba?\([^)]*\)|[a-z]+))"), QRegularExpression::CaseInsensitiveOption);
    if (const auto body = bodyTag.match(m_html); body.hasMatch()) {
        auto m = bgStyle.match(body.captured(0));
        if (!m.hasMatch()) {
            m = bgAttr.match(body.captured(0));
        }
        if (m.hasMatch()) {
            const QColor c = QColor::fromString(m.captured(1));
            if (c.isValid()) {
                m_pageColour = c;
            }
        }
    }
    const QByteArray bytes = m_html.toUtf8();
    m_document = litehtml::document::createFromUTF8(bytes.constData(), this, m_context.get(), &user);
    m_renderedWidth = -1;
    relayout();
}

void HtmlView::relayout()
{
    if (!m_document) {
        return;
    }
    const int w = qMax(100, int(width()));
    if (w != m_renderedWidth) {
        m_renderedWidth = w;
        m_document->render(w);
    }
    const int h = m_document->height();
    const int cw = m_document->width();
    if (h != m_contentHeight || cw != m_contentWidth) {
        m_contentHeight = h;
        m_contentWidth = cw;
        Q_EMIT contentHeightChanged();
    }
    update();
}

void HtmlView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() != oldGeometry.width()) {
        relayout();
    }
}

void HtmlView::paint(QPainter *painter)
{
    if (!m_document) {
        return;
    }
    m_painter = painter;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->translate(0, -m_contentY);
    litehtml::position clip(0, int(m_contentY), int(width()), int(height()) + 1);
    m_document->draw(reinterpret_cast<litehtml::uint_ptr>(painter), 0, 0, &clip);
    m_painter = nullptr;
    m_clips.clear();
}

void HtmlView::drawSlice(QPainter *painter, int top, int bottom)
{
    m_painter = painter;
    m_slice = QRect(0, top, qMax(int(width()), m_contentWidth), bottom - top);
    painter->setClipRect(m_slice);
    litehtml::position clip(m_slice.x(), m_slice.y(), m_slice.width(), m_slice.height());
    m_document->draw(reinterpret_cast<litehtml::uint_ptr>(painter), 0, 0, &clip);
    m_painter = nullptr;
    m_clips.clear();
    m_slice = QRect();
}

// The top of the first sheet. Without a painter it only measures.
int HtmlView::drawHeading(QPainter *painter, int width, const QString &subject, const QStringList &lines) const
{
    QFont title(QStringLiteral("Albert Sans"));
    title.setPixelSize(20);
    title.setWeight(QFont::DemiBold);
    QFont plain(QStringLiteral("Albert Sans"));
    plain.setPixelSize(13);
    int y = 0;
    // A subject or a list of recipients that runs on and on stops after a
    // few lines instead of pushing the message off the first sheet.
    const auto put = [&](const QFont &font, const QString &text, const QColor &colour, int maxLines) {
        const QFontMetrics metrics(font);
        const int limit = metrics.lineSpacing() * maxLines;
        const int height = qMin(limit, metrics.boundingRect(QRect(0, 0, width, limit), Qt::TextWordWrap, text).height());
        if (painter) {
            painter->setFont(font);
            painter->setPen(colour);
            painter->drawText(QRect(0, y, width, height), Qt::TextWordWrap, text);
        }
        y += height;
    };
    put(title, subject, QColor(0x1f, 0x21, 0x25), 3);
    y += 8;
    for (const QString &line : lines) {
        put(plain, line, QColor(0x5b, 0x62, 0x75), 4);
        y += 2;
    }
    y += 10;
    if (painter) {
        painter->fillRect(QRect(0, y, width, 1), QColor(0x5b, 0x62, 0x75));
    }
    return y + 17;
}

bool HtmlView::print()
{
    if (!m_document || !m_message) {
        return true;
    }
    // Everything is taken from the message now: the one in the pane can
    // change while the print dialog is open.
    const QString subject = m_message->subject().isEmpty() ? tr("(No subject)") : m_message->subject();
    const QString from = m_message->fromName().isEmpty() || m_message->fromName() == m_message->fromAddress()
        ? m_message->fromAddress()
        : QStringLiteral("%1 <%2>").arg(m_message->fromName(), m_message->fromAddress());
    QStringList lines = {tr("From: %1").arg(from), tr("To: %1").arg(m_message->to())};
    if (!m_message->cc().isEmpty()) {
        lines << tr("Cc: %1").arg(m_message->cc());
    }
    lines << tr("Sent: %1").arg(m_message->date());

    // A second copy of the message, laid out for the paper. It is given the
    // pictures already showing in the pane and fetches none of its own.
    HtmlView page;
    page.m_message = m_message;
    page.m_html = m_html;
    page.m_images = m_images;
    page.m_paper = true;
    page.m_fontSize = 14.5;
    page.setWidth(720);
    page.rebuild();
    if (!page.m_document) {
        return true;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(subject);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(tr("Print"));
    // How many sheets there are depends on the paper chosen in the dialog.
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (dialog.exec() != QDialog::Accepted) {
        return true;
    }
    QPainter painter;
    if (!painter.begin(&printer)) {
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // The message is laid out as it is on screen, 96 to the inch, and
    // scaled up to the printer's dots. Mail that insists on being wider
    // than the paper (a newsletter with a fixed width) is shrunk to fit.
    const QRectF sheet = printer.pageRect(QPrinter::DevicePixel);
    const int width = qMax(100, qRound(sheet.width() * 96.0 / printer.resolution()));
    page.setWidth(width);
    page.relayout();
    const int laidOut = qMax(width, page.m_contentWidth);
    const qreal scale = sheet.width() / laidOut;
    const int sheetHeight = qMax(200, int(sheet.height() / scale));
    const int total = page.m_contentHeight;
    const int heading = qMin(drawHeading(nullptr, laidOut, subject, lines), sheetHeight / 2);

    // Drawn once onto nothing, to learn where the lines and pictures are.
    QList<QPair<int, int>> boxes;
    {
        QImage nothing(1, 1, QImage::Format_ARGB32_Premultiplied);
        QPainter measure(&nothing);
        page.m_boxes = &boxes;
        page.drawSlice(&measure, 0, total);
        page.m_boxes = nullptr;
    }

    // Each sheet ends at the paper's height, pulled up to the top of any
    // line or picture it would otherwise cut through.
    QList<int> ends;
    int top = 0;
    int room = sheetHeight - heading;
    while (top < total) {
        int bottom = top + room;
        if (bottom >= total) {
            bottom = total;
        } else {
            for (bool moved = true; moved;) {
                moved = false;
                for (const auto &[start, end] : std::as_const(boxes)) {
                    // Only what fits on a sheet of its own can be kept whole.
                    if (start > top && start < bottom && end > bottom && end - start < sheetHeight) {
                        bottom = start;
                        moved = true;
                    }
                }
            }
        }
        ends.append(bottom);
        top = bottom;
        room = sheetHeight;
    }
    if (ends.isEmpty()) {
        ends.append(0);
    }

    for (int i = 0; i < ends.size(); ++i) {
        if (i > 0) {
            printer.newPage();
        }
        painter.save();
        painter.scale(scale, scale);
        const int start = i == 0 ? 0 : ends.at(i - 1);
        if (i == 0) {
            drawHeading(&painter, laidOut, subject, lines);
            painter.translate(0, heading);
        }
        painter.translate(0, -start);
        if (ends.at(i) > start) {
            page.drawSlice(&painter, start, ends.at(i));
        }
        painter.restore();
    }
    painter.end();
    return true;
}

void HtmlView::hoverMoveEvent(QHoverEvent *event)
{
    if (!m_document) {
        return;
    }
    litehtml::position::vector redraw;
    const QPointF p = event->position();
    m_document->on_mouse_over(int(p.x()), int(p.y() + m_contentY), int(p.x()), int(p.y()), redraw);
    if (!redraw.empty()) {
        update();
    }
}

void HtmlView::hoverLeaveEvent(QHoverEvent *)
{
    if (!m_document) {
        return;
    }
    litehtml::position::vector redraw;
    m_document->on_mouse_leave(redraw);
    set_cursor("auto");
    update();
}

void HtmlView::mousePressEvent(QMouseEvent *event)
{
    if (!m_document) {
        event->ignore();
        return;
    }
    if (event->button() == Qt::RightButton) {
        Q_EMIT contextRequested(m_hoveredLink, event->position().x(), event->position().y());
        return;
    }
    litehtml::position::vector redraw;
    const QPointF p = event->position();
    m_document->on_lbutton_down(int(p.x()), int(p.y() + m_contentY), int(p.x()), int(p.y()), redraw);
    m_pressedLink = m_hoveredLink;
    // Only links take the click; anything else lets the pane scroll.
    if (m_hoveredLink.isEmpty()) {
        event->ignore();
    }
}

void HtmlView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_document || event->button() != Qt::LeftButton) {
        return;
    }
    litehtml::position::vector redraw;
    const QPointF p = event->position();
    m_document->on_lbutton_up(int(p.x()), int(p.y() + m_contentY), int(p.x()), int(p.y()), redraw);
    update();
}

litehtml::uint_ptr HtmlView::create_font(const litehtml::tchar_t *faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration,
                                         litehtml::font_metrics *fm)
{
    QStringList families;
    for (QString family : utf8(faceName).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        family = family.trimmed().remove(QLatin1Char('"')).remove(QLatin1Char('\''));
        const QString lower = family.toLower();
        if (lower == QLatin1String("serif") || lower.contains(QLatin1String("times")) || lower == QLatin1String("georgia")) {
            families << QStringLiteral("Noto Serif");
        } else if (lower == QLatin1String("monospace") || lower.contains(QLatin1String("courier")) || lower.contains(QLatin1String("consolas"))) {
            families << QStringLiteral("Noto Sans Mono");
        } else if (lower == QLatin1String("sans-serif") || lower.contains(QLatin1String("arial")) || lower.contains(QLatin1String("helvetica"))
                   || lower.contains(QLatin1String("segoe")) || lower == QLatin1String("system-ui") || lower == QLatin1String("-apple-system")) {
            families << QStringLiteral("Albert Sans");
        } else {
            families << family;
        }
    }
    families << QStringLiteral("Albert Sans");
    QFont font;
    font.setFamilies(families);
    font.setPixelSize(qMax(1, size));
    font.setWeight(QFont::Weight(qBound(100, weight, 900)));
    font.setItalic(italic == litehtml::fontStyleItalic);
    font.setUnderline(decoration & litehtml::font_decoration_underline);
    font.setStrikeOut(decoration & litehtml::font_decoration_linethrough);
    font.setOverline(decoration & litehtml::font_decoration_overline);
    const QFontMetrics metrics(font);
    if (fm) {
        fm->ascent = metrics.ascent();
        fm->descent = metrics.descent();
        fm->height = metrics.height();
        fm->x_height = metrics.xHeight();
        fm->draw_spaces = font.underline() || font.strikeOut() || font.overline();
    }
    const litehtml::uint_ptr handle = m_nextFont++;
    m_fonts.insert(handle, font);
    return handle;
}

void HtmlView::delete_font(litehtml::uint_ptr hFont)
{
    m_fonts.remove(hFont);
}

QFont HtmlView::fontFor(litehtml::uint_ptr handle) const
{
    return m_fonts.value(handle, QFont(QStringLiteral("Albert Sans")));
}

int HtmlView::text_width(const litehtml::tchar_t *text, litehtml::uint_ptr hFont)
{
    return QFontMetrics(fontFor(hFont)).horizontalAdvance(utf8(text));
}

void HtmlView::draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t *text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position &pos)
{
    auto *painter = reinterpret_cast<QPainter *>(hdc);
    const QFont font = fontFor(hFont);
    painter->setFont(font);
    painter->setPen(colourOf(color));
    painter->drawText(pos.x, pos.y + QFontMetrics(font).ascent(), utf8(text));
    if (m_boxes) {
        m_boxes->append(qMakePair(int(pos.y), int(pos.y + pos.height)));
    }
}

int HtmlView::pt_to_px(int pt) const
{
    return qRound(pt * 96.0 / 72.0);
}

int HtmlView::get_default_font_size() const
{
    return qRound(m_fontSize);
}

const litehtml::tchar_t *HtmlView::get_default_font_name() const
{
    return "Albert Sans";
}

void HtmlView::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker)
{
    auto *painter = reinterpret_cast<QPainter *>(hdc);
    const QRectF r(marker.pos.x, marker.pos.y, marker.pos.width, marker.pos.height);
    painter->setPen(QPen(colourOf(marker.color), 1));
    switch (marker.marker_type) {
    case litehtml::list_style_type_disc:
        painter->setBrush(colourOf(marker.color));
        painter->drawEllipse(r);
        break;
    case litehtml::list_style_type_circle:
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(r);
        break;
    case litehtml::list_style_type_square:
        painter->fillRect(r, colourOf(marker.color));
        break;
    default:
        break;
    }
    painter->setBrush(Qt::NoBrush);
}

QString HtmlView::resolve(const QString &src) const
{
    if (m_baseUrl.isEmpty() || src.contains(QLatin1Char(':'))) {
        return src;
    }
    return QUrl(m_baseUrl).resolved(QUrl(src)).toString();
}

void HtmlView::load_image(const litehtml::tchar_t *src, const litehtml::tchar_t *, bool)
{
    const QString url = resolve(utf8(src));
    if (url.isEmpty() || m_images.contains(url)) {
        return;
    }
    if (m_message) {
        const QImage image = m_message->inlineImage(url);
        if (!image.isNull()) {
            m_images.insert(url, image);
            return;
        }
    }
    // Pictures from the web only with the person's say-so, and trackers
    // never: "Show pictures" shows pictures, not the read receipt hidden
    // among them.
    if (m_allowRemote && (url.startsWith(QLatin1String("https://")) || url.startsWith(QLatin1String("http://")))
        && !(m_message && m_message->isTracker(url))) {
        fetch(url);
    }
}

void HtmlView::fetch(const QString &url)
{
    if (m_fetching.contains(url)) {
        return;
    }
    m_fetching.insert(url);
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setCookieJar(nullptr);
        m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    QNetworkRequest request{QUrl(url)};
    // Nothing that tells the sender more than the picture's address already
    // does: no cookies, no referrer, a browser-like agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network->get(request);
    const QString html = m_html;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, html]() {
        reply->deleteLater();
        if (html != m_html || reply->error() != QNetworkReply::NoError) {
            return;
        }
        const QImage image = QImage::fromData(reply->read(8 * 1024 * 1024));
        if (image.isNull()) {
            return;
        }
        m_images.insert(url, image);
        // A picture arriving can change the layout around it.
        m_renderedWidth = -1;
        relayout();
    });
}

void HtmlView::get_image_size(const litehtml::tchar_t *src, const litehtml::tchar_t *, litehtml::size &sz)
{
    const QImage image = m_images.value(resolve(utf8(src)));
    sz.width = image.width();
    sz.height = image.height();
}

void HtmlView::draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint &bg)
{
    auto *painter = reinterpret_cast<QPainter *>(hdc);
    // The page itself: transparent over the window's glass for plain mail,
    // white for designed mail.
    if (bg.is_root) {
        // The page colour fills the whole message, not just the part
        // litehtml thinks of as the window.
        const QRect page(0, 0, int(width()), m_contentHeight);
        if (m_paper) {
            painter->fillRect(page, Qt::white);
        }
        if (bg.color.alpha > 0 && m_paper) {
            painter->fillRect(page, colourOf(bg.color));
        } else if (m_pageColour.isValid() && m_paper) {
            painter->fillRect(page, m_pageColour);
        }
        if (bg.image.empty()) {
            return;
        }
    }
    const QRectF box(bg.border_box.x, bg.border_box.y, bg.border_box.width, bg.border_box.height);
    if (m_boxes && !bg.image.empty()) {
        m_boxes->append(qMakePair(int(bg.border_box.y), int(bg.border_box.y + bg.border_box.height)));
    }
    painter->save();
    painter->setClipRect(QRect(bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height), Qt::IntersectClip);
    if (bg.color.alpha > 0 && !bg.is_root) {
        if (hasRadius(bg.border_radius)) {
            painter->fillPath(roundedPath(box, bg.border_radius), colourOf(bg.color));
        } else {
            painter->fillRect(box, colourOf(bg.color));
        }
    }
    if (!bg.image.empty()) {
        const QImage image = m_images.value(resolve(utf8(bg.image.c_str())));
        if (!image.isNull() && bg.image_size.width > 0 && bg.image_size.height > 0) {
            const QImage scaled = image.scaled(bg.image_size.width, bg.image_size.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            const QRect origin(bg.origin_box.x, bg.origin_box.y, bg.origin_box.width, bg.origin_box.height);
            const QPoint start(origin.x() + bg.position_x, origin.y() + bg.position_y);
            switch (bg.repeat) {
            case litehtml::background_repeat_no_repeat:
                painter->drawImage(start, scaled);
                break;
            case litehtml::background_repeat_repeat_x:
                painter->drawTiledPixmap(QRect(bg.clip_box.x, start.y(), bg.clip_box.width, scaled.height()), QPixmap::fromImage(scaled),
                                         QPoint((bg.clip_box.x - start.x()) % qMax(1, scaled.width()), 0));
                break;
            case litehtml::background_repeat_repeat_y:
                painter->drawTiledPixmap(QRect(start.x(), bg.clip_box.y, scaled.width(), bg.clip_box.height), QPixmap::fromImage(scaled),
                                         QPoint(0, (bg.clip_box.y - start.y()) % qMax(1, scaled.height())));
                break;
            default:
                painter->drawTiledPixmap(QRect(bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height), QPixmap::fromImage(scaled),
                                         QPoint((bg.clip_box.x - start.x()) % qMax(1, scaled.width()), (bg.clip_box.y - start.y()) % qMax(1, scaled.height())));
                break;
            }
        }
    }
    painter->restore();
}

void HtmlView::draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders, const litehtml::position &draw_pos, bool)
{
    auto *painter = reinterpret_cast<QPainter *>(hdc);
    const QRectF r(draw_pos.x, draw_pos.y, draw_pos.width, draw_pos.height);
    const auto penFor = [](const litehtml::border &b) {
        QPen pen(colourOf(b.color), b.width);
        pen.setCapStyle(Qt::FlatCap);
        if (b.style == litehtml::border_style_dashed) {
            pen.setStyle(Qt::DashLine);
        } else if (b.style == litehtml::border_style_dotted) {
            pen.setStyle(Qt::DotLine);
        }
        return pen;
    };
    const auto visible = [](const litehtml::border &b) {
        return b.width > 0 && b.style != litehtml::border_style_none && b.style != litehtml::border_style_hidden && b.color.alpha > 0;
    };
    const bool uniform = borders.left.width == borders.top.width && borders.top.width == borders.right.width && borders.right.width == borders.bottom.width
        && colourOf(borders.left.color) == colourOf(borders.top.color) && colourOf(borders.top.color) == colourOf(borders.right.color)
        && colourOf(borders.right.color) == colourOf(borders.bottom.color);
    painter->save();
    if (uniform && visible(borders.top) && hasRadius(borders.radius)) {
        const qreal half = borders.top.width / 2.0;
        painter->setPen(penFor(borders.top));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(roundedPath(r.adjusted(half, half, -half, -half), borders.radius));
    } else {
        if (visible(borders.top)) {
            painter->setPen(penFor(borders.top));
            const qreal y = r.top() + borders.top.width / 2.0;
            painter->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
        if (visible(borders.bottom)) {
            painter->setPen(penFor(borders.bottom));
            const qreal y = r.bottom() - borders.bottom.width / 2.0;
            painter->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
        if (visible(borders.left)) {
            painter->setPen(penFor(borders.left));
            const qreal x = r.left() + borders.left.width / 2.0;
            painter->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        if (visible(borders.right)) {
            painter->setPen(penFor(borders.right));
            const qreal x = r.right() - borders.right.width / 2.0;
            painter->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
    }
    painter->restore();
}

void HtmlView::set_caption(const litehtml::tchar_t *)
{
}

void HtmlView::set_base_url(const litehtml::tchar_t *base_url)
{
    m_baseUrl = utf8(base_url);
}

void HtmlView::link(const std::shared_ptr<litehtml::document> &, const litehtml::element::ptr &)
{
}

void HtmlView::on_anchor_click(const litehtml::tchar_t *url, const litehtml::element::ptr &)
{
    const QString target = resolve(utf8(url));
    if (!target.isEmpty() && !target.startsWith(QLatin1Char('#'))) {
        Q_EMIT linkActivated(target);
    }
}

void HtmlView::set_cursor(const litehtml::tchar_t *cursor)
{
    const bool pointer = qstrcmp(cursor, "pointer") == 0;
    setCursor(pointer ? Qt::PointingHandCursor : Qt::ArrowCursor);
    // litehtml only says "pointer" over a link; the address itself comes
    // from the element under the mouse.
    QString link;
    if (pointer && m_document) {
        // The status line shows where a link really goes, the way a browser
        // does, which is how a disguised link gives itself away.
        const QPointF p = mapFromGlobal(QCursor::pos());
        litehtml::element::ptr el = m_document->root() ? m_document->root()->get_element_by_point(int(p.x()), int(p.y() + m_contentY), int(p.x()), int(p.y())) : nullptr;
        while (el) {
            if (const char *tag = el->get_tagName(); tag && qstricmp(tag, "a") == 0) {
                link = resolve(utf8(el->get_attr("href")));
                break;
            }
            el = el->parent();
        }
    }
    if (link != m_hoveredLink) {
        m_hoveredLink = link;
        Q_EMIT hoveredLinkChanged();
    }
}

void HtmlView::transform_text(litehtml::tstring &text, litehtml::text_transform tt)
{
    QString s = QString::fromUtf8(text.c_str());
    switch (tt) {
    case litehtml::text_transform_uppercase:
        s = s.toUpper();
        break;
    case litehtml::text_transform_lowercase:
        s = s.toLower();
        break;
    case litehtml::text_transform_capitalize:
        if (!s.isEmpty()) {
            s[0] = s[0].toUpper();
        }
        break;
    default:
        break;
    }
    text = s.toStdString();
}

// Style sheets from the web would tell the sender the message was opened,
// just like pictures, so none are fetched.
void HtmlView::import_css(litehtml::tstring &text, const litehtml::tstring &, litehtml::tstring &)
{
    text.clear();
}

void HtmlView::set_clip(const litehtml::position &pos, const litehtml::border_radiuses &, bool valid_x, bool valid_y)
{
    QRect clip(pos.x, pos.y, pos.width, pos.height);
    if (!valid_x) {
        clip.setLeft(-100000);
        clip.setRight(100000);
    }
    if (!valid_y) {
        clip.setTop(-1000000);
        clip.setBottom(1000000);
    }
    if (!m_clips.isEmpty()) {
        clip = clip.intersected(m_clips.top());
    } else if (m_slice.isValid()) {
        clip = clip.intersected(m_slice);
    }
    m_clips.push(clip);
    if (m_painter) {
        m_painter->setClipRect(clip);
    }
}

void HtmlView::del_clip()
{
    if (!m_clips.isEmpty()) {
        m_clips.pop();
    }
    if (m_painter) {
        if (m_clips.isEmpty() && m_slice.isValid()) {
            m_painter->setClipRect(m_slice);
        } else if (m_clips.isEmpty()) {
            m_painter->setClipping(false);
        } else {
            m_painter->setClipRect(m_clips.top());
        }
    }
}

void HtmlView::get_client_rect(litehtml::position &client) const
{
    client.x = 0;
    client.y = 0;
    client.width = int(width());
    client.height = int(height());
}

std::shared_ptr<litehtml::element> HtmlView::create_element(const litehtml::tchar_t *, const litehtml::string_map &, const std::shared_ptr<litehtml::document> &)
{
    return nullptr;
}

void HtmlView::get_media_features(litehtml::media_features &media) const
{
    media.type = litehtml::media_type_screen;
    media.width = int(width());
    media.height = int(height());
    media.device_width = int(width());
    media.device_height = int(height());
    media.color = 8;
    media.monochrome = 0;
    media.color_index = 256;
    media.resolution = 96;
}

void HtmlView::get_language(litehtml::tstring &language, litehtml::tstring &culture) const
{
    language = "en";
    culture.clear();
}
