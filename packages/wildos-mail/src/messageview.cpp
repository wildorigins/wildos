#include "messageview.h"
#include "formats.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextDocumentFragment>
#include <QUrl>

#include <camel/camel.h>

#include "mailengine.h"

namespace
{
QString str(const gchar *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

QByteArray decoded(CamelDataWrapper *wrapper)
{
    GByteArray *bytes = g_byte_array_new();
    CamelStream *stream = camel_stream_mem_new_with_byte_array(bytes);
    camel_data_wrapper_decode_to_stream_sync(wrapper, stream, nullptr, nullptr);
    QByteArray out(reinterpret_cast<const char *>(bytes->data), int(bytes->len));
    g_object_unref(stream);
    return out;
}

QString textOf(CamelMimePart *part)
{
    CamelDataWrapper *content = camel_medium_get_content(CAMEL_MEDIUM(part));
    const QByteArray bytes = decoded(content);
    const char *charset = camel_content_type_param(camel_mime_part_get_content_type(part), "charset");
    QStringDecoder decoder(charset && *charset ? charset : "UTF-8");
    if (!decoder.isValid()) {
        decoder = QStringDecoder(QStringDecoder::Utf8);
    }
    QString text = decoder(bytes);
    if (decoder.hasError() && (!charset || !*charset)) {
        text = QString::fromLatin1(bytes);
    }
    return text;
}

QString formatList(CamelInternetAddress *list, QString *addresses)
{
    QStringList names;
    QStringList addrs;
    if (!list) {
        return {};
    }
    for (int i = 0; i < camel_address_length(CAMEL_ADDRESS(list)); ++i) {
        const gchar *n = nullptr;
        const gchar *a = nullptr;
        camel_internet_address_get(list, i, &n, &a);
        names << (n && *n ? str(n) : str(a));
        if (addresses) {
            addrs << (n && *n ? QStringLiteral("%1 <%2>").arg(str(n), str(a)) : str(a));
        }
    }
    if (addresses) {
        *addresses = addrs.join(QStringLiteral(", "));
    }
    return names.join(QStringLiteral(", "));
}

struct Parsed {
    QString html;
    QString plain;
    // An invitation's iCalendar (text/calendar), wherever it was.
    QString calendar;
    QHash<QString, QByteArray> inlineParts;
    struct Att {
        QString name;
        QString mime;
        QByteArray data;
    };
    QList<Att> attachments;
};

void walk(CamelMimePart *part, Parsed &out)
{
    CamelDataWrapper *content = camel_medium_get_content(CAMEL_MEDIUM(part));
    CamelContentType *type = camel_mime_part_get_content_type(part);
    if (!content) {
        return;
    }
    if (CAMEL_IS_MULTIPART(content)) {
        CamelMultipart *multi = CAMEL_MULTIPART(content);
        const guint count = camel_multipart_get_number(multi);
        if (camel_content_type_is(type, "multipart", "alternative")) {
            // The richest version: HTML (possibly with its pictures in a
            // multipart/related), else the last one.
            CamelMimePart *best = nullptr;
            for (guint i = 0; i < count; ++i) {
                CamelMimePart *child = camel_multipart_get_part(multi, i);
                CamelContentType *ct = camel_mime_part_get_content_type(child);
                if (camel_content_type_is(ct, "text", "html") || camel_content_type_is(ct, "multipart", "related")) {
                    best = child;
                }
                if (camel_content_type_is(ct, "text", "plain") && out.plain.isEmpty()) {
                    out.plain = textOf(child);
                }
                // Invitations carry the event as one of the versions.
                if (camel_content_type_is(ct, "text", "calendar") && out.calendar.isEmpty()) {
                    out.calendar = textOf(child);
                }
            }
            if (!best && count > 0) {
                best = camel_multipart_get_part(multi, count - 1);
            }
            if (best) {
                walk(best, out);
            }
            return;
        }
        for (guint i = 0; i < count; ++i) {
            walk(camel_multipart_get_part(multi, i), out);
        }
        return;
    }

    const char *disposition = camel_mime_part_get_disposition(part);
    const bool attached = disposition && g_ascii_strcasecmp(disposition, "attachment") == 0;
    const QString filename = str(camel_mime_part_get_filename(part));
    const QString contentId = str(camel_mime_part_get_content_id(part));
    gchar *simple = camel_content_type_simple(type);
    const QString mime = str(simple).toLower();
    g_free(simple);

    if ((mime == QLatin1String("text/calendar") || mime == QLatin1String("application/ics")) && out.calendar.isEmpty()) {
        out.calendar = textOf(part);
        if (!attached) {
            return;
        }
    }
    if (!attached && mime == QLatin1String("text/html") && out.html.isEmpty()) {
        out.html = textOf(part);
        return;
    }
    if (!attached && mime == QLatin1String("text/plain") && filename.isEmpty() && out.html.isEmpty()) {
        if (out.plain.isEmpty()) {
            out.plain = textOf(part);
        } else {
            out.plain += QStringLiteral("\n\n") + textOf(part);
        }
        return;
    }
    const QByteArray data = decoded(content);
    if (!contentId.isEmpty() && mime.startsWith(QLatin1String("image/"))) {
        out.inlineParts.insert(contentId, data);
        if (!attached) {
            return;
        }
    }
    QString name = filename;
    if (name.isEmpty()) {
        if (CAMEL_IS_MIME_MESSAGE(content)) {
            name = str(camel_mime_message_get_subject(CAMEL_MIME_MESSAGE(content))) + QStringLiteral(".eml");
        } else {
            const QMimeType t = QMimeDatabase().mimeTypeForName(mime);
            name = MessageView::tr("Attachment") + (t.isValid() && !t.preferredSuffix().isEmpty() ? QLatin1Char('.') + t.preferredSuffix() : QString());
        }
    }
    out.attachments.append({name, mime, data});
}

const QRegularExpression &remoteImageRe()
{
    static const QRegularExpression re(QStringLiteral(R"(<img\b[^>]*?\bsrc\s*=\s*["']?(https?://[^"'\s>]+)[^>]*>)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// What gives a web picture away as a tracker: one or two pixels in size,
// hidden, or an address shaped like the open-tracking services'.
bool trackerTag(const QString &tag, const QString &url)
{
    static const QRegularExpression tiny(QStringLiteral(R"(\b(width|height)\s*=\s*["']?[0-2](px)?["'\s>/])"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tinyStyle(QStringLiteral(R"((width|height)\s*:\s*[0-2]px|display\s*:\s*none|visibility\s*:\s*hidden)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trackUrl(QStringLiteral(R"((/open(\.gif|\.php|\.aspx)?(\?|/|$)|/track(ing)?/|/pixel|/beacon|/wf/open|/e/o/|/o\.gif|mailtrack|list-manage\.com/track|/emimp/|/imp\?))"),
                                             QRegularExpression::CaseInsensitiveOption);
    return tiny.match(tag).hasMatch() || tinyStyle.match(tag).hasMatch() || trackUrl.match(url).hasMatch();
}

QString stripActive(QString html)
{
    static const QRegularExpression blocks(QStringLiteral(R"(<(script|iframe|object|embed|frameset|applet)\b.*?</\1\s*>)"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression singles(QStringLiteral(R"(<(script|iframe|object|embed|link|meta|base)\b[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    html.remove(blocks);
    html.remove(singles);
    return html;
}

// Mail that sets its own colours or lays itself out in tables was designed
// on white, so it is shown on white.
bool designedOnWhite(const QString &html)
{
    static const QRegularExpression styled(QStringLiteral(R"(bgcolor|background|<table|[^-]color\s*[:=])"), QRegularExpression::CaseInsensitiveOption);
    return styled.match(html).hasMatch();
}
}

MessageView::MessageView(QObject *parent)
    : QObject(parent)
{
}

void MessageView::setKey(const QString &key)
{
    if (key == m_key) {
        return;
    }
    m_key = key;
    m_showPictures = false;
    Q_EMIT keyChanged();
    Q_EMIT showPicturesChanged();
    load();
}

void MessageView::setShowPictures(bool show)
{
    if (show != m_showPictures) {
        m_showPictures = show;
        Q_EMIT showPicturesChanged();
    }
}

QString MessageView::date() const
{
    if (!m_date.isValid()) {
        return {};
    }
    return Formats::names().dayName(m_date.date().dayOfWeek(), QLocale::ShortFormat) + QLatin1Char(' ') + Formats::shortDate(m_date.date())
        + QLatin1Char(' ') + Formats::time(m_date);
}

void MessageView::load()
{
    const int generation = ++m_generation;
    m_attachments.clear();
    m_calendar.clear();
    m_attachmentData.clear();
    m_inline.clear();
    m_trackerUrls.clear();
    m_html.clear();
    m_plain.clear();
    m_error.clear();
    m_remoteImages = 0;
    m_trackers = 0;
    if (m_key.isEmpty()) {
        m_loading = false;
        Q_EMIT loaded();
        return;
    }
    m_loading = true;
    Q_EMIT loaded();

    QString accountId, folderName, uid;
    MailEngine *engine = MailEngine::instance();
    if (!MailEngine::splitKey(m_key, &accountId, &folderName, &uid)) {
        return;
    }
    if (const Account *a = engine->accountData(accountId)) {
        m_accountName = engine->accountList().size() > 1 ? a->name : QString();
    }

    struct Result {
        QString error;
        QString subject, fromName, fromAddress, replyTo, to, cc, toAddresses, ccAddresses, messageId, references;
        QDateTime date;
        Parsed parsed;
    };
    auto result = std::make_shared<Result>();
    const bool outbox = folderName == QLatin1String(".outbox");
    engine->run([engine, accountId, folderName, uid, result, outbox]() {
        GError *error = nullptr;
        CamelMimeMessage *message = nullptr;
        if (outbox) {
            message = camel_mime_message_new();
            const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/outbox/") + accountId + QLatin1Char('/') + uid;
            CamelStream *stream = camel_stream_fs_new_with_name(path.toUtf8().constData(), O_RDONLY, 0, &error);
            if (!stream || !camel_data_wrapper_construct_from_stream_sync(CAMEL_DATA_WRAPPER(message), stream, nullptr, &error)) {
                g_clear_object(&message);
            }
            g_clear_object(&stream);
        } else if (CamelFolder *folder = engine->folderSync(accountId, folderName, &error)) {
            message = camel_folder_get_message_sync(folder, uid.toUtf8().constData(), nullptr, &error);
        }
        if (!message) {
            result->error = MailEngine::plainError(error, MessageView::tr("Mail couldn't get it from your email provider. Try again in a moment."));
            g_clear_error(&error);
            return;
        }
        result->subject = str(camel_mime_message_get_subject(message));
        CamelInternetAddress *from = camel_mime_message_get_from(message);
        if (from && camel_address_length(CAMEL_ADDRESS(from)) > 0) {
            const gchar *n = nullptr;
            const gchar *a = nullptr;
            camel_internet_address_get(from, 0, &n, &a);
            result->fromName = str(n).isEmpty() ? str(a) : str(n);
            result->fromAddress = str(a);
        }
        if (CamelInternetAddress *replyTo = camel_mime_message_get_reply_to(message); replyTo && camel_address_length(CAMEL_ADDRESS(replyTo)) > 0) {
            formatList(replyTo, &result->replyTo);
        }
        result->to = formatList(camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO), &result->toAddresses);
        result->cc = formatList(camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_CC), &result->ccAddresses);
        result->messageId = str(camel_mime_message_get_message_id(message));
        result->references = str(camel_medium_get_header(CAMEL_MEDIUM(message), "References"));
        gint offset = 0;
        const gint64 when = camel_mime_message_get_date(message, &offset);
        result->date = when > 0 ? QDateTime::fromSecsSinceEpoch(when) : QDateTime();
        walk(CAMEL_MIME_PART(message), result->parsed);
        g_object_unref(message);
    }, [this, generation, result]() {
        if (generation != m_generation) {
            return;
        }
        m_loading = false;
        m_error = result->error;
        m_subject = result->subject;
        m_fromName = result->fromName;
        m_fromAddress = result->fromAddress;
        m_replyTo = result->replyTo;
        m_to = result->to;
        m_cc = result->cc;
        m_toAddresses = result->toAddresses;
        m_ccAddresses = result->ccAddresses;
        m_messageId = result->messageId;
        m_references = result->references;
        m_date = result->date;
        m_inline = result->parsed.inlineParts;

        const Parsed &p = result->parsed;
        m_calendar = p.calendar;
        if (!p.html.isEmpty()) {
            m_html = stripActive(p.html);
            m_paper = designedOnWhite(m_html);
            m_plain = p.plain.isEmpty() ? QTextDocumentFragment::fromHtml(m_html).toPlainText() : p.plain;
        } else {
            m_plain = p.plain;
            m_html = plainToHtml(p.plain);
            m_paper = false;
        }
        auto it = remoteImageRe().globalMatch(m_html);
        while (it.hasNext()) {
            const auto m = it.next();
            ++m_remoteImages;
            if (trackerTag(m.captured(0), m.captured(1))) {
                m_trackerUrls.insert(m.captured(1));
            }
        }
        m_trackers = m_trackerUrls.size();
        MailEngine::instance()->countTrackers(m_trackers, m_messageId.isEmpty() ? m_key : m_messageId);

        for (int i = 0; i < p.attachments.size(); ++i) {
            const auto &a = p.attachments[i];
            m_attachmentData.append({a.name, a.mime, a.data});
            m_attachments.append(QVariantMap{{QStringLiteral("name"), a.name},
                                             {QStringLiteral("size"), QLocale().formattedDataSize(a.data.size(), 0, QLocale::DataSizeTraditionalFormat)},
                                             {QStringLiteral("type"), a.mime},
                                             {QStringLiteral("index"), i}});
        }
        Q_EMIT loaded();
    });
}

QImage MessageView::inlineImage(const QString &src) const
{
    if (src.startsWith(QLatin1String("cid:"), Qt::CaseInsensitive)) {
        const QString id = src.mid(4);
        auto it = m_inline.constFind(id);
        if (it == m_inline.cend()) {
            it = m_inline.constFind(QUrl::fromPercentEncoding(id.toUtf8()));
        }
        return it == m_inline.cend() ? QImage() : QImage::fromData(it.value());
    }
    if (src.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
        const int comma = src.indexOf(QLatin1Char(','));
        if (comma > 0 && src.left(comma).endsWith(QLatin1String(";base64"), Qt::CaseInsensitive)) {
            return QImage::fromData(QByteArray::fromBase64(src.mid(comma + 1).toLatin1()));
        }
    }
    return {};
}

QString MessageView::writeAttachment(int index, const QString &dir) const
{
    if (index < 0 || index >= m_attachmentData.size()) {
        return {};
    }
    const Attachment &a = m_attachmentData[index];
    QString name = QFileInfo(a.name).fileName();
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    if (name.isEmpty()) {
        name = tr("Attachment");
    }
    QDir().mkpath(dir);
    QString path = dir + QLatin1Char('/') + name;
    const QFileInfo info(path);
    for (int n = 2; QFile::exists(path); ++n) {
        path = dir + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(" (%1)").arg(n)
            + (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix());
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(a.data);
    return path;
}

void MessageView::openAttachment(int index)
{
    // Opened from a private folder only this user can read, so another
    // account on the PC can't pick it up.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/open");
    const QString path = writeAttachment(index, dir);
    if (!path.isEmpty()) {
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MessageView::saveAttachment(int index)
{
    const QString path = writeAttachment(index, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (!path.isEmpty()) {
        Q_EMIT attachmentSaved(path);
    }
}

void MessageView::saveAllAttachments()
{
    QString last;
    for (int i = 0; i < m_attachmentData.size(); ++i) {
        last = writeAttachment(i, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    }
    if (!last.isEmpty()) {
        Q_EMIT attachmentSaved(QFileInfo(last).absolutePath());
    }
}

QVariantMap MessageView::replyData(const QString &mode) const
{
    QString accountId, folder, uid;
    MailEngine::splitKey(m_key, &accountId, &folder, &uid);
    const Account *account = MailEngine::instance()->accountData(accountId);
    const QString self = account ? account->email.toLower() : QString();

    QVariantMap data;
    data.insert(QStringLiteral("accountId"), accountId);
    QString subject = m_subject;
    static const QRegularExpression prefix(QStringLiteral(R"(^\s*(re|fw|fwd|aw|sv|antw)\s*:\s*)"), QRegularExpression::CaseInsensitiveOption);
    if (mode == QLatin1String("forward")) {
        if (!subject.contains(QRegularExpression(QStringLiteral(R"(^\s*(fw|fwd)\s*:)"), QRegularExpression::CaseInsensitiveOption))) {
            subject = QStringLiteral("Fw: ") + subject;
        }
    } else if (!prefix.match(subject).hasMatch()) {
        subject = QStringLiteral("Re: ") + subject;
    }
    data.insert(QStringLiteral("subject"), subject);

    if (mode != QLatin1String("forward")) {
        const QString sender = m_replyTo.isEmpty() ? (m_fromName.isEmpty() || m_fromName == m_fromAddress ? m_fromAddress : QStringLiteral("%1 <%2>").arg(m_fromName, m_fromAddress)) : m_replyTo;
        QStringList to{sender};
        QStringList cc;
        if (mode == QLatin1String("replyAll")) {
            for (const QString &a : m_toAddresses.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
                if (!a.toLower().contains(self) && !a.toLower().contains(m_fromAddress.toLower())) {
                    to << a;
                }
            }
            for (const QString &a : m_ccAddresses.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
                if (!a.toLower().contains(self)) {
                    cc << a;
                }
            }
        }
        data.insert(QStringLiteral("to"), to);
        data.insert(QStringLiteral("cc"), cc);
        data.insert(QStringLiteral("inReplyTo"), m_messageId);
        data.insert(QStringLiteral("references"), (m_references + QLatin1Char(' ') + QLatin1Char('<') + m_messageId + QLatin1Char('>')).trimmed());
    }
    const QString when = date();
    const QString header = mode == QLatin1String("forward")
        ? tr("From: %1\nSent: %2\nTo: %3\nSubject: %4").arg(m_fromName.isEmpty() ? m_fromAddress : QStringLiteral("%1 <%2>").arg(m_fromName, m_fromAddress), when, m_to, m_subject)
        : tr("On %1, %2 wrote:").arg(when, m_fromName.isEmpty() ? m_fromAddress : m_fromName);
    data.insert(QStringLiteral("quoteHeader"), header);
    data.insert(QStringLiteral("quote"), m_plain.trimmed());
    if (mode == QLatin1String("forward")) {
        data.insert(QStringLiteral("forwardKey"), m_key);
    }
    return data;
}

void MessageView::copyText() const
{
    copyToClipboard(m_plain);
}

void MessageView::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString MessageView::plainToHtml(const QString &text)
{
    static const QRegularExpression url(QStringLiteral(R"((https?://[^\s<>"]+[^\s<>".,;:!?)\]]))"));
    QString out;
    bool inQuote = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        const bool quoted = line.startsWith(QLatin1Char('>'));
        if (quoted && !inQuote) {
            out += QStringLiteral("<blockquote>");
        } else if (!quoted && inQuote) {
            out += QStringLiteral("</blockquote>");
        }
        inQuote = quoted;
        if (quoted) {
            line = line.mid(1);
            if (line.startsWith(QLatin1Char(' '))) {
                line = line.mid(1);
            }
        }
        QString escaped = line.toHtmlEscaped();
        escaped.replace(url, QStringLiteral(R"(<a href="\1">\1</a>)"));
        out += escaped + QStringLiteral("<br>");
    }
    if (inQuote) {
        out += QStringLiteral("</blockquote>");
    }
    return QStringLiteral("<div class=\"plain\">") + out + QStringLiteral("</div>");
}
