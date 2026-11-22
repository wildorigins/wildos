#include "senderimages.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDnsLookup>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <memory>

#include "peopleengine.h"

namespace
{
constexpr int badgeSize = 96;
constexpr qint64 month = 30LL * 24 * 60 * 60;
constexpr int maxRunning = 3;
constexpr int timeoutMs = 8000;
// BIMI asks for a small SVG Tiny file; anything bigger isn't one.
constexpr qint64 bimiCap = 64 * 1024;
constexpr qint64 pageCap = 256 * 1024;
constexpr qint64 iconCap = 256 * 1024;
const QString settingKey = QStringLiteral("privacy/logos");

QString cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-mail");
}

QString logoDir()
{
    return cacheRoot() + QStringLiteral("/logos");
}

// Suffixes under which companies register a third label, so that
// mail.example.co.uk belongs to example.co.uk and not to co.uk.
const QSet<QString> &twoPartSuffixes()
{
    static const QSet<QString> set = {
        QStringLiteral("co.uk"), QStringLiteral("org.uk"), QStringLiteral("ac.uk"), QStringLiteral("gov.uk"),
        QStringLiteral("me.uk"), QStringLiteral("ltd.uk"), QStringLiteral("plc.uk"), QStringLiteral("net.uk"),
        QStringLiteral("nhs.uk"), QStringLiteral("sch.uk"), QStringLiteral("com.au"), QStringLiteral("net.au"),
        QStringLiteral("org.au"), QStringLiteral("edu.au"), QStringLiteral("gov.au"), QStringLiteral("co.nz"),
        QStringLiteral("org.nz"), QStringLiteral("net.nz"), QStringLiteral("co.jp"), QStringLiteral("ne.jp"),
        QStringLiteral("or.jp"), QStringLiteral("co.za"), QStringLiteral("org.za"), QStringLiteral("com.br"),
        QStringLiteral("com.mx"), QStringLiteral("co.in"), QStringLiteral("net.in"), QStringLiteral("org.in"),
        QStringLiteral("com.sg"), QStringLiteral("com.hk"), QStringLiteral("com.cn"), QStringLiteral("com.tr"),
        QStringLiteral("co.kr"), QStringLiteral("com.ar"), QStringLiteral("co.il"), QStringLiteral("com.my"),
        QStringLiteral("com.ph"), QStringLiteral("co.id"), QStringLiteral("com.tw"), QStringLiteral("com.pl"),
    };
    return set;
}

const QSet<QString> &freeDomains()
{
    static const QSet<QString> set = {
        QStringLiteral("gmail.com"), QStringLiteral("googlemail.com"), QStringLiteral("msn.com"),
        QStringLiteral("ymail.com"), QStringLiteral("icloud.com"), QStringLiteral("me.com"),
        QStringLiteral("mac.com"), QStringLiteral("aol.com"), QStringLiteral("proton.me"),
        QStringLiteral("protonmail.com"), QStringLiteral("pm.me"), QStringLiteral("mail.com"),
        QStringLiteral("zoho.com"), QStringLiteral("fastmail.com"), QStringLiteral("fastmail.fm"),
    };
    return set;
}

// Free mail under many country domains: hotmail.co.uk, yahoo.fr and so on.
const QSet<QString> &freeFamilies()
{
    static const QSet<QString> set = {
        QStringLiteral("hotmail"), QStringLiteral("outlook"), QStringLiteral("live"),
        QStringLiteral("yahoo"), QStringLiteral("gmx"), QStringLiteral("yandex"),
    };
    return set;
}

QString orgDomain(const QString &domain)
{
    const QStringList labels = domain.split(QLatin1Char('.'));
    const int n = labels.size();
    if (n <= 2) {
        return domain;
    }
    const QString lastTwo = labels[n - 2] + QLatin1Char('.') + labels[n - 1];
    if (twoPartSuffixes().contains(lastTwo)) {
        return labels.mid(n - 3).join(QLatin1Char('.'));
    }
    return lastTwo;
}

bool isFree(const QString &domain)
{
    const QString org = orgDomain(domain);
    return freeDomains().contains(domain) || freeDomains().contains(org)
        || freeFamilies().contains(org.section(QLatin1Char('.'), 0, 0));
}

bool validDomain(const QString &domain)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9-]+(\\.[a-z0-9-]+)+$"));
    return domain.size() <= 253 && re.match(domain).hasMatch();
}

QImage circled(QImage square)
{
    QImage mask(square.size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawEllipse(QRectF(0, 0, square.width(), square.height()));
    }
    QPainter p(&square);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, mask);
    p.end();
    return square;
}

void drawCropped(QPainter &p, const QImage &source)
{
    const int side = std::min(source.width(), source.height());
    const QRect from((source.width() - side) / 2, (source.height() - side) / 2, side, side);
    p.drawImage(QRect(0, 0, badgeSize, badgeSize), source, from);
}

// The largest picture in the file, or none under 32 pixels: a 16 pixel
// favicon blown up to a badge is a smudge, and initials read better.
QImage decodeLogo(const QByteArray &body)
{
    const QByteArray head = body.left(2048);
    if (head.contains("<svg") && !head.contains("<html") && !head.contains("<HTML")) {
        QSvgRenderer renderer(body);
        if (!renderer.isValid()) {
            return {};
        }
        QSize size = renderer.defaultSize();
        if (size.isEmpty()) {
            size = QSize(256, 256);
        }
        size.scale(256, 256, Qt::KeepAspectRatio);
        QImage out(size, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        renderer.render(&p);
        p.end();
        return out;
    }
    QBuffer buffer;
    buffer.setData(body);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize declared = reader.size();
    if (declared.isValid() && (declared.width() > 4096 || declared.height() > 4096)) {
        return {};
    }
    QImage best;
    for (int i = 0; i < 16; ++i) {
        const QImage image = reader.read();
        if (image.isNull()) {
            break;
        }
        if (image.width() * image.height() > best.width() * best.height()) {
            best = image;
        }
        if (!reader.jumpToNextImage()) {
            break;
        }
    }
    if (best.isNull() || std::min(best.width(), best.height()) < 32) {
        return {};
    }
    return best;
}

bool writePng(const QImage &image, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QImageWriter writer(&file, "png");
    return writer.write(image) && file.commit();
}

// <link rel="icon"> and <link rel="apple-touch-icon"> in a home page, the
// largest first, as long as they're on https.
QList<QUrl> iconLinks(const QByteArray &html, const QUrl &base)
{
    QString text = QString::fromUtf8(html);
    const int headEnd = text.indexOf(QLatin1String("</head"), 0, Qt::CaseInsensitive);
    if (headEnd > 0) {
        text.truncate(headEnd);
    }
    static const QRegularExpression linkRe(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRe(QStringLiteral("([a-zA-Z:-]+)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+))"));
    static const QRegularExpression sizeRe(QStringLiteral("(\\d+)x(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    struct Candidate {
        QUrl url;
        int size;
    };
    QList<Candidate> found;
    auto links = linkRe.globalMatch(text);
    while (links.hasNext()) {
        const QString tag = links.next().captured(0);
        QHash<QString, QString> attrs;
        auto it = attrRe.globalMatch(tag);
        while (it.hasNext()) {
            const auto m = it.next();
            QString value = m.captured(2);
            if (value.isNull()) {
                value = m.captured(3);
            }
            if (value.isNull()) {
                value = m.captured(4);
            }
            attrs.insert(m.captured(1).toLower(), value);
        }
        const QStringList rels = attrs.value(QStringLiteral("rel")).toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const bool touch = rels.contains(QLatin1String("apple-touch-icon")) || rels.contains(QLatin1String("apple-touch-icon-precomposed"));
        if (!touch && !rels.contains(QLatin1String("icon"))) {
            continue;
        }
        QString href = attrs.value(QStringLiteral("href")).trimmed();
        href.replace(QLatin1String("&amp;"), QLatin1String("&"));
        if (href.isEmpty()) {
            continue;
        }
        const QUrl url = base.resolved(QUrl(href));
        if (url.scheme() != QLatin1String("https") || url.host().isEmpty()) {
            continue;
        }
        const QString type = attrs.value(QStringLiteral("type")).toLower();
        const QString path = url.path().toLower();
        const bool svg = type.contains(QLatin1String("svg")) || path.endsWith(QLatin1String(".svg"));
        const bool ico = type.contains(QLatin1String("icon")) || path.endsWith(QLatin1String(".ico"));
        const bool png = type.contains(QLatin1String("png")) || path.endsWith(QLatin1String(".png"));
        if (!type.isEmpty() && !svg && !ico && !png) {
            continue;
        }
        int size = 0;
        const QString sizes = attrs.value(QStringLiteral("sizes")).toLower();
        if (sizes.contains(QLatin1String("any"))) {
            size = 512;
        }
        auto sm = sizeRe.globalMatch(sizes);
        while (sm.hasNext()) {
            const auto m = sm.next();
            size = std::max(size, std::min(m.captured(1).toInt(), m.captured(2).toInt()));
        }
        if (!sizes.isEmpty() && size > 0 && size < 32) {
            continue;
        }
        if (size == 0) {
            size = svg ? 512 : touch ? 180 : ico ? 32 : 16;
        }
        found.append({url, size});
    }
    std::stable_sort(found.begin(), found.end(), [](const Candidate &a, const Candidate &b) {
        return a.size > b.size;
    });
    QList<QUrl> out;
    for (const Candidate &c : std::as_const(found)) {
        if (!out.contains(c.url)) {
            out << c.url;
        }
        if (out.size() == 3) {
            break;
        }
    }
    return out;
}
}

// One domain's search, step by step: its BIMI record, then (for an
// organisational domain) its website's touch icon, the icons its home page
// names, and its favicon. The first picture that decodes at 32 pixels or
// more wins.
class LogoFetch : public QObject
{
public:
    using Done = std::function<void(LogoFetch *, const QString &state, bool transient)>;

    LogoFetch(const QString &domain, bool subdomain, QNetworkAccessManager *net, Done done, QObject *parent)
        : QObject(parent)
        , m_domain(domain)
        , m_host(domain)
        , m_subdomain(subdomain)
        , m_net(net)
        , m_done(std::move(done))
    {
    }

    QString domain() const
    {
        return m_domain;
    }

    void start()
    {
        m_dns = new QDnsLookup(QDnsLookup::TXT, QStringLiteral("default._bimi.") + m_domain, this);
        connect(m_dns, &QDnsLookup::finished, this, &LogoFetch::onBimi);
        // The resolver's own retries can take far longer than a logo is worth.
        QTimer::singleShot(timeoutMs, this, [this]() {
            if (m_dns && !m_dns->isFinished()) {
                m_dns->abort();
            }
        });
        m_dns->lookup();
    }

    // Stops without a word to the owner, for the setting turned off.
    void cancel()
    {
        m_cancelled = true;
        if (m_dns) {
            m_dns->abort();
        }
        if (m_reply) {
            m_reply->abort();
        }
        deleteLater();
    }

private:
    void onBimi()
    {
        QUrl logo;
        if (m_dns->error() == QDnsLookup::NoError) {
            for (const QDnsTextRecord &record : m_dns->textRecords()) {
                const QString text = QString::fromUtf8(record.values().join()).trimmed();
                if (!text.startsWith(QLatin1String("v=BIMI1"), Qt::CaseInsensitive)) {
                    continue;
                }
                for (const QString &tag : text.split(QLatin1Char(';'))) {
                    const QString t = tag.trimmed();
                    if (t.startsWith(QLatin1String("l="), Qt::CaseInsensitive)) {
                        logo = QUrl(t.mid(2).trimmed());
                    }
                }
                break;
            }
        } else if (m_dns->error() != QDnsLookup::NotFoundError) {
            m_dnsTransient = true;
        }
        m_dns->deleteLater();
        m_dns = nullptr;
        if (m_cancelled) {
            return;
        }
        if (logo.isValid() && logo.scheme() == QLatin1String("https")) {
            tryImage(logo, bimiCap, [this]() {
                afterBimi();
            });
        } else {
            afterBimi();
        }
    }

    void afterBimi()
    {
        if (m_subdomain) {
            finish(QStringLiteral("alias"));
            return;
        }
        tryImage(site(QStringLiteral("/apple-touch-icon.png")), iconCap, [this]() {
            tryHome();
        });
    }

    void tryHome()
    {
        get(site(QStringLiteral("/")), pageCap, QByteArrayLiteral("text/html"), [this](const QByteArray &body, const QUrl &url, bool responded) {
            // Some companies only run their site on www.
            if (!responded && !m_host.startsWith(QLatin1String("www."))) {
                m_host = QStringLiteral("www.") + m_domain;
                tryHome();
                return;
            }
            if (!body.isEmpty()) {
                m_candidates = iconLinks(body, url);
            }
            tryCandidate();
        });
    }

    void tryCandidate()
    {
        if (m_candidates.isEmpty()) {
            tryImage(site(QStringLiteral("/favicon.ico")), iconCap, [this]() {
                finish(QStringLiteral("none"));
            });
            return;
        }
        tryImage(m_candidates.takeFirst(), iconCap, [this]() {
            tryCandidate();
        });
    }

    QUrl site(const QString &path) const
    {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(m_host);
        url.setPath(path);
        return url;
    }

    void get(const QUrl &url, qint64 cap, const QByteArray &accept, std::function<void(const QByteArray &, const QUrl &, bool)> then)
    {
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setMaximumRedirectsAllowed(5);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setTransferTimeout(timeoutMs);
        // What any browser sends; nothing about Mail or the person.
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64)"));
        request.setRawHeader("Accept", accept);
        request.setRawHeader("Accept-Language", "en");
        QNetworkReply *reply = m_net->get(request);
        m_reply = reply;
        auto body = std::make_shared<QByteArray>();
        auto over = std::make_shared<bool>(false);
        connect(reply, &QNetworkReply::readyRead, this, [reply, body, over, cap]() {
            body->append(reply->readAll());
            if (body->size() > cap) {
                *over = true;
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, body, over, cap, then]() {
            reply->deleteLater();
            m_reply = nullptr;
            if (m_cancelled) {
                return;
            }
            if (!*over) {
                body->append(reply->readAll());
            }
            const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const bool responded = status.isValid();
            m_anyResponse = m_anyResponse || responded;
            const bool ok = !*over && body->size() <= cap && reply->error() == QNetworkReply::NoError && status.toInt() == 200
                && reply->url().scheme() == QLatin1String("https");
            then(ok ? *body : QByteArray(), reply->url(), responded);
        });
    }

    void tryImage(const QUrl &url, qint64 cap, std::function<void()> otherwise)
    {
        get(url, cap, QByteArrayLiteral("image/png,image/svg+xml,image/x-icon,image/*;q=0.8,*/*;q=0.5"),
            [this, otherwise](const QByteArray &body, const QUrl &, bool) {
                if (body.isEmpty()) {
                    otherwise();
                    return;
                }
                // Decoding and drawing stay off the window's thread.
                QPointer<LogoFetch> self(this);
                const QString path = SenderImages::logoPath(m_domain);
                QThreadPool::globalInstance()->start([self, body, path, otherwise]() {
                    const QImage logo = decodeLogo(body);
                    const bool saved = !logo.isNull() && writePng(SenderImages::logoBadge(logo), path);
                    QMetaObject::invokeMethod(SenderImages::instance(), [self, saved, otherwise]() {
                        if (!self || self->m_cancelled) {
                            return;
                        }
                        if (saved) {
                            self->finish(QStringLiteral("hit"));
                        } else {
                            otherwise();
                        }
                    }, Qt::QueuedConnection);
                });
            });
    }

    void finish(const QString &state)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        // Offline, every domain would look like it has no logo; don't keep
        // that for a month.
        const bool transient = state != QLatin1String("hit") && m_dnsTransient && !m_anyResponse;
        m_done(this, state, transient);
        deleteLater();
    }

    QString m_domain;
    QString m_host;
    bool m_subdomain;
    QNetworkAccessManager *m_net;
    Done m_done;
    QDnsLookup *m_dns = nullptr;
    QPointer<QNetworkReply> m_reply;
    QList<QUrl> m_candidates;
    bool m_anyResponse = false;
    bool m_dnsTransient = false;
    bool m_cancelled = false;
    bool m_finished = false;
};

SenderImages *SenderImages::instance()
{
    static SenderImages *images = new SenderImages();
    return images;
}

SenderImages *SenderImages::create(QQmlEngine *, QJSEngine *)
{
    SenderImages *images = instance();
    QJSEngine::setObjectOwnership(images, QJSEngine::CppOwnership);
    return images;
}

SenderImages::SenderImages(QObject *parent)
    : QObject(parent)
{
    m_logos = QSettings().value(settingKey, true).toBool();
    m_net = new QNetworkAccessManager(this);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &SenderImages::saveIndex);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_saveTimer.isActive()) {
            m_saveTimer.stop();
            saveIndex();
        }
    });
    loadIndex();
}

QString SenderImages::photoDir()
{
    return cacheRoot() + QStringLiteral("/photos");
}

QString SenderImages::logoPath(const QString &domain)
{
    return logoDir() + QLatin1Char('/') + domain + QStringLiteral(".png");
}

void SenderImages::setLogos(bool on)
{
    if (on == m_logos) {
        return;
    }
    m_logos = on;
    QSettings().setValue(settingKey, on);
    if (!on) {
        for (const QPointer<LogoFetch> &fetch : std::as_const(m_running)) {
            if (fetch) {
                fetch->cancel();
            }
        }
        m_running.clear();
        m_queue.clear();
        m_busy.clear();
    }
    Q_EMIT logosChanged();
    bump();
}

QString SenderImages::imageFor(const QString &address, bool fetch)
{
    // The address books are read when the first sender shows, a moment
    // after the window opens rather than holding up the start.
    if (!m_peopleStarted) {
        m_peopleStarted = true;
        QTimer::singleShot(2500, this, []() {
            PeopleEngine::instance();
        });
    }
    const QString a = address.trimmed().toLower();
    const QString photo = m_photos.value(a);
    if (!photo.isEmpty()) {
        return QUrl::fromLocalFile(photo).toString();
    }
    const int at = a.lastIndexOf(QLatin1Char('@'));
    if (!m_logos || at < 0) {
        return {};
    }
    const QString domain = a.mid(at + 1);
    if (!validDomain(domain) || isFree(domain)) {
        return {};
    }
    return logoFor(domain, fetch, true);
}

QString SenderImages::logoFor(const QString &domain, bool fetch, bool followAlias)
{
    const auto it = m_index.constFind(domain);
    if (it == m_index.cend()) {
        if (fetch) {
            enqueue(domain);
        }
        return {};
    }
    if (fetch && QDateTime::currentSecsSinceEpoch() - it->time > month) {
        enqueue(domain);
    }
    if (it->state == QLatin1String("hit")) {
        const QString path = logoPath(domain);
        if (QFileInfo::exists(path)) {
            return QUrl::fromLocalFile(path).toString();
        }
        if (fetch) {
            enqueue(domain);
        }
        return {};
    }
    if (it->state == QLatin1String("alias") && followAlias) {
        const QString org = orgDomain(domain);
        return org != domain ? logoFor(org, fetch, false) : QString();
    }
    return {};
}

void SenderImages::enqueue(const QString &domain)
{
    if (m_busy.contains(domain) || m_tried.contains(domain)) {
        return;
    }
    m_busy.insert(domain);
    m_queue.append(domain);
    pump();
}

void SenderImages::pump()
{
    m_running.removeAll(QPointer<LogoFetch>());
    while (m_running.size() < maxRunning && !m_queue.isEmpty() && m_logos) {
        const QString domain = m_queue.takeFirst();
        auto *fetch = new LogoFetch(domain, orgDomain(domain) != domain, m_net,
                                    [this, domain](LogoFetch *f, const QString &state, bool transient) {
                                        finished(f, domain, state, transient);
                                    },
                                    this);
        m_running.append(fetch);
        fetch->start();
    }
}

void SenderImages::finished(LogoFetch *fetch, const QString &domain, const QString &state, bool transient)
{
    m_running.removeAll(QPointer<LogoFetch>(fetch));
    m_busy.remove(domain);
    m_tried.insert(domain);
    if (!transient) {
        m_index.insert(domain, {QDateTime::currentSecsSinceEpoch(), state});
        m_saveTimer.start();
        if (state != QLatin1String("none")) {
            bump();
        }
    }
    QTimer::singleShot(0, this, &SenderImages::pump);
}

void SenderImages::setContactPhotos(const QString &book, const QHash<QString, QString> &photos)
{
    QSet<QString> before;
    for (const auto &list : std::as_const(m_photosByBook)) {
        for (const QString &path : list) {
            before.insert(path);
        }
    }
    if (photos.isEmpty()) {
        m_photosByBook.remove(book);
    } else {
        m_photosByBook.insert(book, photos);
    }
    QHash<QString, QString> merged;
    QSet<QString> after;
    for (const auto &list : std::as_const(m_photosByBook)) {
        for (auto it = list.cbegin(); it != list.cend(); ++it) {
            merged.insert(it.key(), it.value());
            after.insert(it.value());
        }
    }
    // A photo that was changed or taken off leaves its old file behind.
    for (const QString &path : std::as_const(before)) {
        if (!after.contains(path)) {
            QFile::remove(path);
        }
    }
    if (merged != m_photos) {
        m_photos = merged;
        bump();
    }
}

void SenderImages::loadIndex()
{
    QFile file(logoDir() + QStringLiteral("/index.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        const QString state = o.value(QStringLiteral("s")).toString();
        if (!validDomain(it.key()) || (state != QLatin1String("hit") && state != QLatin1String("none") && state != QLatin1String("alias"))) {
            continue;
        }
        m_index.insert(it.key(), {qint64(o.value(QStringLiteral("t")).toDouble()), state});
    }
}

void SenderImages::saveIndex()
{
    QJsonObject root;
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it) {
        root.insert(it.key(), QJsonObject{{QStringLiteral("t"), double(it->time)}, {QStringLiteral("s"), it->state}});
    }
    QDir().mkpath(logoDir());
    QSaveFile file(logoDir() + QStringLiteral("/index.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void SenderImages::bump()
{
    ++m_revision;
    Q_EMIT changed();
}

QImage SenderImages::photoBadge(const QImage &source)
{
    QImage out(badgeSize, badgeSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::white);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    drawCropped(p, source);
    p.end();
    return circled(out);
}

QImage SenderImages::logoBadge(const QImage &source)
{
    const QImage img = source.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    const auto opaqueAt = [&img](int x, int y) {
        return qAlpha(img.pixel(x, y)) > 200;
    };
    const bool opaqueCorners = opaqueAt(1, 1) && opaqueAt(w - 2, 1) && opaqueAt(1, h - 2) && opaqueAt(w - 2, h - 2);
    const double aspect = double(w) / h;

    const bool fill = opaqueCorners && aspect > 0.8 && aspect < 1.25;
    QColor background(Qt::white);
    if (!fill) {
        // A white logo on a clear background vanishes on white; it gets a
        // dark circle instead.
        qint64 light = 0;
        qint64 counted = 0;
        const int step = std::max(1, std::max(w, h) / 64);
        for (int y = 0; y < h; y += step) {
            for (int x = 0; x < w; x += step) {
                const QRgb c = img.pixel(x, y);
                if (qAlpha(c) > 128) {
                    ++counted;
                    if (qGray(c) > 225) {
                        ++light;
                    }
                }
            }
        }
        if (counted > 0 && light * 10 > counted * 8) {
            background = QColor(0x2b, 0x30, 0x3b);
        }
    }

    QImage out(badgeSize, badgeSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(background);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (fill) {
        drawCropped(p, img);
    } else {
        const QSize size = img.size().scaled(68, 68, Qt::KeepAspectRatio);
        p.drawImage(QRect((badgeSize - size.width()) / 2, (badgeSize - size.height()) / 2, size.width(), size.height()), img);
    }
    p.end();
    return circled(out);
}
