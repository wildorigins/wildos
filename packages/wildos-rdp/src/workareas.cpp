#include "workareas.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
const char *objectPath = "/org/wildos/RemoteApp/WorkArea";

// KWin's Workspace has no signal for its work area changing, so the script
// listens for what changes it: screens coming, going or changing, and a
// panel (a dock window) appearing, going, moving, resizing or hiding. A
// change of several of those at once is reported once, as it settles.
const char *script = R"(import QtQuick
import org.kde.kwin as KWin

Item {
    id: root

    readonly property var target: %1
    property string last: ""
    property var watchers: []

    function areas() {
        const out = [];
        for (const output of KWin.Workspace.screens) {
            const a = KWin.Workspace.clientArea(KWin.Workspace.MaximizeArea, output, KWin.Workspace.currentDesktop);
            if (a && a.width > 0 && a.height > 0) {
                out.push({ name: String(output.name), x: a.x, y: a.y, width: a.width, height: a.height });
            }
        }
        return JSON.stringify(out);
    }

    function report() {
        const now = root.areas();
        if (now === root.last) {
            return;
        }
        root.last = now;
        reporter.arguments = [now];
        reporter.call();
    }

    function watch(window) {
        if (!window || window.deleted || !window.dock) {
            return;
        }
        root.watchers.push(watcher.createObject(root, { target: window }));
    }

    function unwatch(window) {
        root.watchers = root.watchers.filter(w => {
            if (w.target === window || !w.target) {
                w.destroy();
                return false;
            }
            return true;
        });
    }

    KWin.DBusCall {
        id: reporter
        service: root.target.service
        path: root.target.path
        dbusInterface: "org.wildos.RemoteApp.WorkArea"
        method: "Report"
    }

    Timer {
        id: settle
        interval: 200
        onTriggered: root.report()
    }

    Component {
        id: watcher
        Connections {
            function onFrameGeometryChanged() { settle.restart(); }
            function onHiddenChanged() { settle.restart(); }
        }
    }

    Connections {
        target: KWin.Workspace
        function onScreensChanged() { settle.restart(); }
        function onVirtualScreenGeometryChanged() { settle.restart(); }
        function onCurrentDesktopChanged() { settle.restart(); }
        function onWindowAdded(window) {
            if (window.dock) {
                root.watch(window);
                settle.restart();
            }
        }
        function onWindowRemoved(window) {
            if (window.dock) {
                root.unwatch(window);
                settle.restart();
            }
        }
    }

    Component.onCompleted: {
        KWin.Workspace.windows.forEach(w => root.watch(w));
        root.report();
    }
}
)";

QDBusMessage scripting(const QString &method, const QVariantList &args)
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"), method);
    m.setArguments(args);
    return m;
}
}

WorkAreas::WorkAreas(QObject *parent)
    : QObject(parent)
    , m_plugin(QStringLiteral("wildos-rdp-workarea-%1").arg(QCoreApplication::applicationPid()))
{
}

WorkAreas::~WorkAreas()
{
    stop();
}

void WorkAreas::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (m_loaded || !bus.isConnected() || dir.isEmpty()) {
        return;
    }
    if (!m_registered) {
        m_registered = bus.registerObject(QString::fromLatin1(objectPath), this, QDBusConnection::ExportAllSlots);
        if (!m_registered) {
            return;
        }
    }
    // The script answers to this process alone, by its own name on the bus:
    // a second session's process has a script of its own.
    const QJsonObject target{{QStringLiteral("service"), bus.baseService()}, {QStringLiteral("path"), QString::fromLatin1(objectPath)}};
    m_file = dir + u'/' + m_plugin + QStringLiteral(".qml");
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QString::fromLatin1(script).arg(QString::fromUtf8(QJsonDocument(target).toJson(QJsonDocument::Compact))).toUtf8());
    if (!file.commit()) {
        return;
    }
    m_loaded = true;
    // Never waited for: the loading window must not wait on KWin. A script
    // of the same name left by an earlier process with this pid goes first.
    bus.asyncCall(scripting(QStringLiteral("unloadScript"), {m_plugin}));
    auto *load = new QDBusPendingCallWatcher(bus.asyncCall(scripting(QStringLiteral("loadDeclarativeScript"), {m_file, m_plugin})), this);
    connect(load, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<int> reply = *call;
        if (reply.isError() || reply.value() < 0) {
            qInfo("wildos-rdp: KWin did not take the work area script; Windows keeps the whole screen until a window is maximised");
            return;
        }
        QDBusMessage run = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(reply.value()),
                                                          QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
        QDBusConnection::sessionBus().asyncCall(run);
    });
}

void WorkAreas::stop()
{
    if (m_loaded) {
        m_loaded = false;
        QDBusConnection::sessionBus().call(scripting(QStringLiteral("unloadScript"), {m_plugin}), QDBus::Block, 500);
        QFile::remove(m_file);
    }
    if (m_registered) {
        m_registered = false;
        QDBusConnection::sessionBus().unregisterObject(QString::fromLatin1(objectPath));
    }
}

void WorkAreas::Report(const QString &json)
{
    QHash<QString, QRectF> areas;
    const QJsonArray list = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        const QRectF r(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble(), o.value(QStringLiteral("width")).toDouble(),
                       o.value(QStringLiteral("height")).toDouble());
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && r.isValid()) {
            areas.insert(name, r);
        }
    }
    if (areas == m_areas) {
        return;
    }
    m_areas = areas;
    Q_EMIT changed();
}
