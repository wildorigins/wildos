#include "display.h"

#include <kscreen/config.h>
#include <kscreen/configmonitor.h>
#include <kscreen/getconfigoperation.h>
#include <kscreen/mode.h>
#include <kscreen/output.h>
#include <kscreen/setconfigoperation.h>

#include <QCoreApplication>
#include <QRect>
#include <QSizeF>

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <optional>

using namespace KScreen;

namespace
{
// Windows gives a new resolution 15 seconds before it switches back.
constexpr int previewSeconds = 15;

// KScreen treats rates this close as the same, so 59.94 and 60 Hz are one
// choice.
bool sameRate(float a, float b)
{
    return std::abs(a - b) < 0.5f;
}

QString sizeKey(const QSize &size)
{
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

QString rateKey(float rate)
{
    return QString::number(double(rate), 'f', 3);
}

bool isBuiltIn(const OutputPtr &output)
{
    return output->type() == Output::Panel;
}

// Largest first, as Windows and kcm_kscreen list them.
QList<QSize> resolutions(const OutputPtr &output)
{
    QList<QSize> sizes;
    const auto modes = output->modes();
    for (const ModePtr &mode : modes) {
        if (!sizes.contains(mode->size())) {
            sizes << mode->size();
        }
    }
    std::sort(sizes.begin(), sizes.end(), [](const QSize &a, const QSize &b) {
        return a.width() > b.width() || (a.width() == b.width() && a.height() > b.height());
    });
    return sizes;
}

QList<float> refreshRates(const OutputPtr &output)
{
    QList<float> rates;
    const ModePtr base = output->currentMode() ? output->currentMode() : output->preferredMode();
    if (!base) {
        return rates;
    }
    const auto modes = output->modes();
    for (const ModePtr &mode : modes) {
        if (mode->size() != base->size()) {
            continue;
        }
        const float rate = mode->refreshRate();
        if (std::none_of(rates.cbegin(), rates.cend(), [rate](float r) {
                return sameRate(r, rate);
            })) {
            rates << rate;
        }
    }
    std::sort(rates.begin(), rates.end(), std::greater<>());
    return rates;
}

// The mode at `size` closest to `rate`, or the fastest one at that size when
// it cannot keep the rate (kcm_kscreen's setResolution).
ModePtr modeFor(const OutputPtr &output, const QSize &size, float rate)
{
    ModePtr fastest;
    const auto modes = output->modes();
    for (const ModePtr &mode : modes) {
        if (mode->size() != size) {
            continue;
        }
        if (sameRate(mode->refreshRate(), rate)) {
            return mode;
        }
        if (!fastest || mode->refreshRate() > fastest->refreshRate()) {
            fastest = mode;
        }
    }
    return fastest;
}

// KWin's own choice for a screen it has not seen before
// (OutputConfigurationStore::chooseScale in Plasma 6.3), so "recommended" is
// the scale the screen started with. A built-in panel is taken to be a
// laptop's.
double recommendedScale(const OutputPtr &output)
{
    const ModePtr mode = output->preferredMode() ? output->preferredMode() : output->currentMode();
    const QSize mm = output->sizeMm();
    if (!mode || mm.width() < 3 || mm.height() < 3) {
        return 1.0;
    }
    const double targetDpi = isBuiltIn(output) ? 125 : 96;
    const double minSize = 800;
    const QSize px = mode->size();

    const double dpiX = px.width() / (mm.width() / 25.4);
    const double maxX = std::clamp(px.width() / minSize, 1.0, 3.0);
    const double scaleX = std::clamp(dpiX / targetDpi, 1.0, maxX);

    const double dpiY = px.height() / (mm.height() / 25.4);
    const double maxY = std::clamp(px.height() / minSize, 1.0, 3.0);
    const double scaleY = std::clamp(dpiY / targetDpi, 1.0, maxY);

    return std::round(100.0 * std::min(scaleX, scaleY) / 5.0) * 5.0 / 100.0;
}

QString label(const OutputPtr &output)
{
    if (isBuiltIn(output)) {
        return QCoreApplication::translate("Displays", "Built-in");
    }
    // Projectors and some TVs report an aspect ratio in place of a size.
    const QSize mm = output->sizeMm();
    if (mm.width() >= 100 && mm.height() >= 100) {
        const int inches = int(std::round(std::hypot(mm.width(), mm.height()) / 25.4));
        return QCoreApplication::translate("Displays", "External, %1 inch").arg(inches);
    }
    return QCoreApplication::translate("Displays", "External");
}
}

Displays::Displays(QObject *parent)
    : QObject(parent)
{
    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, [this] {
        if (--m_secondsLeft <= 0) {
            revert();
            return;
        }
        Q_EMIT confirmChanged();
    });

    // KScreen reports a change once per setting; reading once they settle
    // keeps the page from flickering through the steps.
    m_reloadSoon.setSingleShot(true);
    m_reloadSoon.setInterval(300);
    connect(&m_reloadSoon, &QTimer::timeout, this, [this] {
        if (m_applying > 0) {
            m_reloadSoon.start();
            return;
        }
        reload();
    });
    connect(ConfigMonitor::instance(), &ConfigMonitor::configurationChanged, &m_reloadSoon, qOverload<>(&QTimer::start));

    // A slider sends a value for every step; KWin needs only where it stops.
    m_applySoon.setSingleShot(true);
    m_applySoon.setInterval(150);
    connect(&m_applySoon, &QTimer::timeout, this, &Displays::apply);

    reload();
}

bool Displays::available() const
{
    return m_config && m_config->supportedFeatures().testFlag(Config::Feature::Writable) && !m_outputs.isEmpty();
}

void Displays::reload()
{
    if (m_loading) {
        m_reloadAgain = true;
        return;
    }
    m_loading = true;
    connect(new GetConfigOperation(), &GetConfigOperation::finished, this, [this](ConfigOperation *op) {
        m_loading = false;
        m_ready = true;
        if (op->hasError()) {
            m_config.reset();
        } else {
            m_config = qobject_cast<GetConfigOperation *>(op)->config();
            // As kcm_kscreen's ConfigHandler::initOutput: geometry() is only
            // right once the logical size is set.
            const auto all = m_config->outputs();
            for (const OutputPtr &output : all) {
                output->setExplicitLogicalSize(m_config->logicalSizeForOutput(*output));
            }
        }
        // A screen plugged in or out mid-countdown would make the old
        // settings name screens that are no longer there.
        if (m_revert && (!m_config || m_config->connectedOutputsHash() != m_revert->connectedOutputsHash())) {
            stopCountdown();
        }
        refresh();
        if (m_reloadAgain) {
            m_reloadAgain = false;
            reload();
        }
    });
}

OutputPtr Displays::output(int id) const
{
    if (!m_config) {
        return {};
    }
    const OutputPtr output = m_config->output(id);
    return output && output->isConnected() ? output : OutputPtr();
}

// Windows numbers the built-in screen 1; the rest follow by connector, so a
// screen keeps its number when another becomes the main display.
QList<OutputPtr> Displays::ordered() const
{
    if (!m_config) {
        return {};
    }
    QList<OutputPtr> list = m_config->connectedOutputs().values();
    std::sort(list.begin(), list.end(), [](const OutputPtr &a, const OutputPtr &b) {
        if (isBuiltIn(a) != isBuiltIn(b)) {
            return isBuiltIn(a);
        }
        return a->name() < b->name();
    });
    return list;
}

QList<OutputPtr> Displays::enabledOutputs() const
{
    QList<OutputPtr> list = ordered();
    list.removeIf([](const OutputPtr &output) {
        return !output->isEnabled();
    });
    return list;
}

// Keys: id, name (connector), number, label, builtIn, enabled, primary,
// x, y, width, height (desktop pixels), rotation, scale, recommendedScale,
// resolutions and rates ([{ value, text }]), resolution, rate, vrrCapable,
// vrr, hdrCapable, hdr, sdrBrightness, sdrMin, sdrMax.
QVariantMap Displays::describe(const OutputPtr &output, int number) const
{
    const QRect geometry = output->geometry();
    const ModePtr current = output->currentMode();
    const ModePtr preferred = output->preferredMode();

    QVariantList sizes;
    const auto sizeList = resolutions(output);
    for (const QSize &size : sizeList) {
        QString text = QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
        if (preferred && preferred->size() == size) {
            text = tr("%1 (recommended)").arg(text);
        }
        sizes << QVariantMap{{QStringLiteral("value"), sizeKey(size)}, {QStringLiteral("text"), text}};
    }

    QVariantList rates;
    QString rate;
    const auto rateList = refreshRates(output);
    for (const float r : rateList) {
        rates << QVariantMap{{QStringLiteral("value"), rateKey(r)}, {QStringLiteral("text"), tr("%1 Hz").arg(int(std::lround(r)))}};
        if (current && rate.isEmpty() && sameRate(r, current->refreshRate())) {
            rate = rateKey(r);
        }
    }

    const auto caps = output->capabilities();
    const bool hdrCapable = caps.testFlag(Output::Capability::HighDynamicRange) && caps.testFlag(Output::Capability::WideColorGamut);
    const double peak = output->maxPeakBrightnessOverride().value_or(output->maxPeakBrightness());

    return {
        {QStringLiteral("id"), output->id()},
        {QStringLiteral("name"), output->name()},
        {QStringLiteral("number"), number},
        {QStringLiteral("label"), label(output)},
        {QStringLiteral("builtIn"), isBuiltIn(output)},
        {QStringLiteral("enabled"), output->isEnabled()},
        {QStringLiteral("primary"), output->isEnabled() && output->priority() == 1},
        {QStringLiteral("x"), geometry.x()},
        {QStringLiteral("y"), geometry.y()},
        {QStringLiteral("width"), geometry.width()},
        {QStringLiteral("height"), geometry.height()},
        {QStringLiteral("rotation"), int(output->rotation())},
        {QStringLiteral("scale"), output->scale()},
        {QStringLiteral("recommendedScale"), recommendedScale(output)},
        {QStringLiteral("resolutions"), sizes},
        {QStringLiteral("resolution"), current ? sizeKey(current->size()) : QString()},
        {QStringLiteral("rates"), rates},
        {QStringLiteral("rate"), rate},
        {QStringLiteral("vrrCapable"), caps.testFlag(Output::Capability::Vrr)},
        {QStringLiteral("vrr"), output->vrrPolicy() != Output::VrrPolicy::Never},
        {QStringLiteral("hdrCapable"), hdrCapable},
        {QStringLiteral("hdr"), output->isHdrEnabled()},
        {QStringLiteral("sdrBrightness"), int(output->sdrBrightness())},
        // kcm_kscreen's "Maximum SDR Brightness" range.
        {QStringLiteral("sdrMin"), 50},
        {QStringLiteral("sdrMax"), peak > 50 ? int(peak) : 500},
    };
}

void Displays::refresh()
{
    QVariantList list;
    const auto all = ordered();
    for (int i = 0; i < all.size(); ++i) {
        list << describe(all[i], i + 1);
    }
    m_outputs = list;

    const auto on = enabledOutputs();
    if (all.size() < 2) {
        m_arrangement = QStringLiteral("single");
    } else if (on.size() == 1) {
        m_arrangement = QStringLiteral("only");
    } else {
        // KWin is not told which screen copies which; a duplicate is screens
        // showing the same part of the desktop, as kcm_kscreen arranges them.
        bool overlap = true;
        for (int i = 0; i < on.size() && overlap; ++i) {
            for (int j = i + 1; j < on.size() && overlap; ++j) {
                overlap = on[i]->geometry().intersects(on[j]->geometry());
            }
        }
        m_arrangement = overlap ? QStringLiteral("duplicate") : QStringLiteral("extend");
    }
    Q_EMIT changed();
}

void Displays::apply()
{
    m_applySoon.stop();
    if (!m_config) {
        return;
    }
    if (!Config::canBeApplied(m_config)) {
        Q_EMIT failed(tr("These screen settings cannot be used."));
        reload();
        return;
    }
    ++m_applying;
    // Not exec(): its nested event loop lets the page run while the config
    // it holds is replaced (KDE bug 466960).
    auto op = new SetConfigOperation(m_config);
    connect(op, &ConfigOperation::finished, this, [this](ConfigOperation *op) {
        --m_applying;
        if (op->hasError()) {
            Q_EMIT failed(op->errorString());
            reload();
        }
    });
}

void Displays::preview(const ConfigPtr &before)
{
    if (!m_revert) {
        m_revert = before;
    }
    m_secondsLeft = previewSeconds;
    m_countdown.start();
    Q_EMIT confirmChanged();
}

void Displays::stopCountdown()
{
    const bool was = bool(m_revert);
    m_revert.reset();
    m_countdown.stop();
    m_secondsLeft = 0;
    if (was) {
        Q_EMIT confirmChanged();
    }
}

void Displays::keep()
{
    stopCountdown();
}

void Displays::revert()
{
    const ConfigPtr target = m_revert;
    stopCountdown();
    if (!target || !Config::canBeApplied(target)) {
        return;
    }
    m_config = target;
    refresh();
    apply();
}

// A screen that grows or shrinks pushes the screens right of and below its
// middle along with its edge, so they stay touching (kcm_kscreen's
// maintainSnapping).
void Displays::resize(const OutputPtr &changed, const QSize &oldSize)
{
    const QSize newSize = m_config->logicalSizeForOutputInt(*changed);
    changed->setExplicitLogicalSize(QSizeF(newSize));
    const QPoint centre = QRect(changed->pos(), oldSize).center();
    const QSize delta = newSize - oldSize;
    const auto all = enabledOutputs();
    for (const OutputPtr &other : all) {
        if (other->id() == changed->id()) {
            continue;
        }
        const QPoint pos = other->pos();
        const QPoint shift(pos.x() >= centre.x() ? delta.width() : 0, pos.y() >= centre.y() ? delta.height() : 0);
        if (!shift.isNull()) {
            other->setPos(pos + shift);
        }
    }
    normalise();
}

// KWin expects the desktop to start at 0, 0.
void Displays::normalise()
{
    const auto on = enabledOutputs();
    if (on.isEmpty()) {
        return;
    }
    int left = INT_MAX;
    int top = INT_MAX;
    for (const OutputPtr &output : on) {
        left = std::min(left, output->pos().x());
        top = std::min(top, output->pos().y());
    }
    for (const OutputPtr &output : on) {
        output->setPos(output->pos() - QPoint(left, top));
    }
}

// Side by side in Windows' numbering, tops level.
void Displays::lineUp()
{
    int x = 0;
    const auto on = enabledOutputs();
    for (const OutputPtr &output : on) {
        output->setReplicationSource(0);
        output->setPos(QPoint(x, 0));
        x += output->geometry().width();
    }
}

void Displays::switchOn(const OutputPtr &output)
{
    if (output->isEnabled()) {
        return;
    }
    if (!output->currentMode()) {
        output->setCurrentModeId(output->preferredModeId());
    }
    if (!output->currentMode()) {
        return;
    }
    int right = 0;
    const auto on = enabledOutputs();
    for (const OutputPtr &other : on) {
        right = std::max(right, other->geometry().x() + other->geometry().width());
    }
    output->setEnabled(true);
    output->setSize(output->currentMode()->size());
    output->setExplicitLogicalSize(QSizeF(m_config->logicalSizeForOutputInt(*output)));
    output->setPos(QPoint(right, 0));
    // A screen that was off has no place in the order; this gives it the
    // next one.
    m_config->adjustPriorities();
}

// kcm_kscreen's setReplicationSourceIndex: the copy takes the mode nearest
// the source's shape and is scaled and placed so it shows exactly the
// source's part of the desktop.
void Displays::mirror(const OutputPtr &replica, const OutputPtr &source)
{
    const ModePtr sourceMode = source->currentMode();
    if (!sourceMode) {
        return;
    }
    const auto turned = [](const OutputPtr &output, QSize size) {
        return output->isHorizontal() ? size : size.transposed();
    };
    const auto ratio = [&turned](const OutputPtr &output, const ModePtr &mode) {
        const QSize size = turned(output, mode->size());
        return size.width() / qreal(size.height());
    };

    const qreal sourceRatio = ratio(source, sourceMode);
    QList<ModePtr> fitting;
    const auto modes = replica->modes();
    for (const ModePtr &mode : modes) {
        if (qFuzzyCompare(ratio(replica, mode), sourceRatio)) {
            fitting << mode;
        }
    }
    ModePtr best = replica->currentMode();
    if (!fitting.isEmpty()) {
        std::sort(fitting.begin(), fitting.end(), [](const ModePtr &a, const ModePtr &b) {
            return a->size().width() < b->size().width();
        });
        const QSize sourceSize = turned(source, sourceMode->size());
        const auto it = std::find_if(fitting.cbegin(), fitting.cend(), [&](const ModePtr &mode) {
            return turned(replica, mode->size()).width() >= sourceSize.width();
        });
        best = it != fitting.cend() ? *it : fitting.last();
    }
    if (!best) {
        return;
    }

    const QSize sourceSize = turned(source, sourceMode->size());
    const QSize replicaSize = turned(replica, best->size());
    qreal scale = source->scale() * std::max(replicaSize.width() / qreal(sourceSize.width()), replicaSize.height() / qreal(sourceSize.height()));
    // Whole 1/120ths, the steps of Wayland's fractional scale, so no sliver
    // is left at the edge to trip hidden panels.
    scale = std::ceil(scale * 120.0) / 120.0;
    const QPoint offset((sourceSize.width() / source->scale() - replicaSize.width() / scale) / 2,
                        (sourceSize.height() / source->scale() - replicaSize.height() / scale) / 2);

    replica->setCurrentModeId(best->id());
    replica->setSize(best->size());
    replica->setScale(scale);
    replica->setExplicitLogicalSize(QSizeF(m_config->logicalSizeForOutputInt(*replica)));
    replica->setReplicationSource(source->id());
    replica->setPos(source->pos() + offset);
}

QPoint Displays::snapped(const OutputPtr &moving, const QPoint &dest, int threshold) const
{
    const QSize size = moving->geometry().size();
    QList<QRect> others;
    const auto on = enabledOutputs();
    for (const OutputPtr &other : on) {
        if (other->id() != moving->id()) {
            others << other->geometry();
        }
    }
    if (others.isEmpty() || size.isEmpty()) {
        return moving->pos();
    }

    std::optional<QPoint> best;
    int bestDistance = INT_MAX;
    const auto consider = [&](const QPoint &candidate) {
        const QRect rect(candidate, size);
        for (const QRect &other : others) {
            if (rect.intersects(other)) {
                return;
            }
        }
        const int distance = (candidate - dest).manhattanLength();
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    };
    const auto align = [threshold](int value, int length, int start, int span) {
        if (std::abs(value - start) < threshold) {
            return start;
        }
        if (std::abs(value + length - (start + span)) < threshold) {
            return start + span - length;
        }
        if (std::abs(value + length / 2 - (start + span / 2)) < threshold) {
            return start + span / 2 - length / 2;
        }
        return value;
    };

    // Every way of touching each other screen along a whole edge, keeping a
    // quarter of the shorter side shared so a corner never counts as
    // touching; the nearest one wins. Screens always end up edge to edge,
    // which KWin needs for the pointer to cross between them.
    for (const QRect &target : others) {
        const int shareY = std::max(1, std::min(size.height(), target.height()) / 4);
        int y = std::clamp(dest.y(), target.y() - size.height() + shareY, target.y() + target.height() - shareY);
        y = align(y, size.height(), target.y(), target.height());
        consider(QPoint(target.x() + target.width(), y));
        consider(QPoint(target.x() - size.width(), y));

        const int shareX = std::max(1, std::min(size.width(), target.width()) / 4);
        int x = std::clamp(dest.x(), target.x() - size.width() + shareX, target.x() + target.width() - shareX);
        x = align(x, size.width(), target.x(), target.width());
        consider(QPoint(x, target.y() + target.height()));
        consider(QPoint(x, target.y() - size.height()));
    }
    return best.value_or(moving->pos());
}

QPoint Displays::snap(int id, int x, int y, int threshold) const
{
    const OutputPtr moving = output(id);
    if (!moving) {
        return QPoint(x, y);
    }
    return snapped(moving, QPoint(x, y), threshold);
}

void Displays::move(int id, int x, int y)
{
    const OutputPtr moving = output(id);
    if (!moving || !moving->isEnabled()) {
        return;
    }
    const auto positions = [this] {
        QList<QPoint> list;
        const auto on = enabledOutputs();
        for (const OutputPtr &output : on) {
            list << output->pos();
        }
        return list;
    };
    const QList<QPoint> before = positions();
    moving->setPos(snapped(moving, QPoint(x, y), 0));
    normalise();
    refresh();
    if (positions() != before) {
        apply();
    }
}

void Displays::setPrimary(int id)
{
    const OutputPtr target = output(id);
    if (!target || !target->isEnabled() || target->priority() == 1) {
        return;
    }
    m_config->setOutputPriority(target, 1);
    refresh();
    apply();
}

void Displays::setResolution(int id, const QString &size)
{
    const OutputPtr target = output(id);
    const QStringList parts = size.split(QLatin1Char('x'));
    if (!target || !target->isEnabled() || parts.size() != 2) {
        return;
    }
    const ModePtr current = target->currentMode();
    const ModePtr mode = modeFor(target, QSize(parts[0].toInt(), parts[1].toInt()), current ? current->refreshRate() : 0);
    if (!mode || (current && mode->id() == current->id())) {
        return;
    }
    const ConfigPtr before = m_config->clone();
    const QSize oldSize = target->explicitLogicalSizeInt();
    target->setCurrentModeId(mode->id());
    target->setSize(mode->size());
    resize(target, oldSize);
    refresh();
    preview(before);
    apply();
}

void Displays::setRefreshRate(int id, const QString &rate)
{
    const OutputPtr target = output(id);
    if (!target || !target->isEnabled() || !target->currentMode()) {
        return;
    }
    const ModePtr current = target->currentMode();
    const float wanted = rate.toFloat();
    if (sameRate(current->refreshRate(), wanted)) {
        return;
    }
    const ModePtr mode = modeFor(target, current->size(), wanted);
    if (!mode || !sameRate(mode->refreshRate(), wanted)) {
        return;
    }
    const ConfigPtr before = m_config->clone();
    target->setCurrentModeId(mode->id());
    refresh();
    preview(before);
    apply();
}

void Displays::setScale(int id, double scale)
{
    const OutputPtr target = output(id);
    if (!target || !target->isEnabled()) {
        return;
    }
    scale = std::clamp(std::round(scale * 120.0) / 120.0, 0.5, 3.0);
    if (qFuzzyCompare(target->scale(), scale)) {
        return;
    }
    const QSize oldSize = target->explicitLogicalSizeInt();
    target->setScale(scale);
    resize(target, oldSize);
    refresh();
    apply();
}

void Displays::setRotation(int id, int rotation)
{
    const OutputPtr target = output(id);
    const auto value = Output::Rotation(rotation);
    if (!target || !target->isEnabled()
        || (value != Output::None && value != Output::Left && value != Output::Inverted && value != Output::Right)
        || target->rotation() == value) {
        return;
    }
    const QSize oldSize = target->explicitLogicalSizeInt();
    target->setRotation(value);
    resize(target, oldSize);
    refresh();
    apply();
}

void Displays::setArrangement(int id, const QString &mode)
{
    const OutputPtr target = output(id);
    if (!target) {
        return;
    }
    const auto all = ordered();
    if (mode == QLatin1String("only")) {
        switchOn(target);
        for (const OutputPtr &other : all) {
            if (other->id() != target->id() && other->isEnabled()) {
                m_config->setOutputPriority(other, 0);
            }
        }
        target->setReplicationSource(0);
        target->setPos(QPoint(0, 0));
    } else if (mode == QLatin1String("off")) {
        if (enabledOutputs().size() < 2 || !target->isEnabled()) {
            return;
        }
        m_config->setOutputPriority(target, 0);
        for (const OutputPtr &other : all) {
            if (other->replicationSource() == target->id()) {
                other->setReplicationSource(0);
            }
        }
    } else if (mode == QLatin1String("extend")) {
        const bool wasDuplicate = m_arrangement == QLatin1String("duplicate");
        for (const OutputPtr &other : all) {
            if (!other->isEnabled()) {
                switchOn(other);
            } else if (wasDuplicate && other->id() != target->id()) {
                // A copy was given its source's shape; on its own it goes
                // back to what suits it.
                if (const ModePtr preferred = other->preferredMode()) {
                    other->setCurrentModeId(preferred->id());
                    other->setSize(preferred->size());
                }
                other->setScale(recommendedScale(other));
                other->setExplicitLogicalSize(QSizeF(m_config->logicalSizeForOutputInt(*other)));
            }
        }
        if (wasDuplicate) {
            lineUp();
        }
    } else if (mode == QLatin1String("duplicate")) {
        switchOn(target);
        target->setReplicationSource(0);
        for (const OutputPtr &other : all) {
            if (other->id() == target->id()) {
                continue;
            }
            switchOn(other);
            mirror(other, target);
        }
    } else {
        return;
    }
    normalise();
    refresh();
    apply();
}

void Displays::setVrr(int id, bool on)
{
    const OutputPtr target = output(id);
    if (!target || !target->capabilities().testFlag(Output::Capability::Vrr)) {
        return;
    }
    // Automatic is KWin's "only for full-screen games and video"; Always
    // would also change the rate under the desktop, which flickers on many
    // screens.
    target->setVrrPolicy(on ? Output::VrrPolicy::Automatic : Output::VrrPolicy::Never);
    refresh();
    apply();
}

void Displays::setHdr(int id, bool on)
{
    const OutputPtr target = output(id);
    if (!target || !target->capabilities().testFlag(Output::Capability::HighDynamicRange) || target->isHdrEnabled() == on) {
        return;
    }
    const ConfigPtr before = m_config->clone();
    // kcm_kscreen switches the wide colour gamut with HDR; KWin's HDR needs
    // both.
    target->setHdrEnabled(on);
    target->setWcgEnabled(on);
    refresh();
    preview(before);
    apply();
}

void Displays::setSdrBrightness(int id, int nits)
{
    const OutputPtr target = output(id);
    if (!target || !target->isHdrEnabled()) {
        return;
    }
    target->setSdrBrightness(uint32_t(std::max(50, nits)));
    refresh();
    m_applySoon.start();
}
