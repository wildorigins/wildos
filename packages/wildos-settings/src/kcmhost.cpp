#include "kcmhost.h"

#include <KCMultiDialog>
#include <KPluginMetaData>
#include <KQuickConfigModule>
#include <KQuickConfigModuleLoader>

namespace
{
std::shared_ptr<QQmlEngine> s_engine;
KcmHost *s_instance = nullptr;

const QString quickNamespace = QStringLiteral("plasma/kcms/systemsettings");
const QString widgetNamespace = QStringLiteral("plasma/kcms/systemsettings_qwidgets");

KPluginMetaData findModule(const QString &pluginId)
{
    KPluginMetaData data = KPluginMetaData::findPluginById(quickNamespace, pluginId);
    if (!data.isValid()) {
        data = KPluginMetaData::findPluginById(widgetNamespace, pluginId);
    }
    return data;
}
}

KcmHost::KcmHost(QObject *parent)
    : QObject(parent)
    , m_engine(s_engine)
{
}

void KcmHost::setEngine(const std::shared_ptr<QQmlEngine> &engine)
{
    s_engine = engine;
}

KcmHost *KcmHost::create(QQmlEngine *qmlEngine, QJSEngine *)
{
    if (!s_instance) {
        s_instance = new KcmHost(qmlEngine);
    }
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

bool KcmHost::available(const QString &pluginId) const
{
    return findModule(pluginId).isValid();
}

bool KcmHost::embeddable(const QString &pluginId) const
{
    return KPluginMetaData::findPluginById(quickNamespace, pluginId).isValid();
}

QObject *KcmHost::load(const QString &pluginId)
{
    const KPluginMetaData data = KPluginMetaData::findPluginById(quickNamespace, pluginId);
    if (!data.isValid()) {
        m_lastError = tr("%1 is not installed").arg(pluginId);
        return nullptr;
    }

    // The module has to share the window's engine: its pages are QML items
    // that end up inside our scene, and items cannot cross engines.
    const auto result = KQuickConfigModuleLoader::loadModule(data, this, {}, m_engine);
    if (!result) {
        m_lastError = result.errorText;
        qWarning() << "Could not load" << pluginId << result.errorText;
        return nullptr;
    }

    KQuickConfigModule *module = result.plugin;
    QQmlEngine::setObjectOwnership(module, QQmlEngine::CppOwnership);
    // The UI has to exist before load(): the module's QML engine is only
    // created with it, and some modules (fonts, region) reach for the
    // engine while loading their settings.
    if (!module->mainUi()) {
        m_lastError = module->errorString();
        module->deleteLater();
        return nullptr;
    }
    module->load();
    m_lastError.clear();
    return module;
}

QString KcmHost::lastError() const
{
    return m_lastError;
}

void KcmHost::release(QObject *module)
{
    if (module) {
        module->deleteLater();
    }
}

void KcmHost::apply(QObject *module)
{
    if (auto kcm = qobject_cast<KQuickConfigModule *>(module)) {
        kcm->save();
    }
}

void KcmHost::revert(QObject *module)
{
    if (auto kcm = qobject_cast<KQuickConfigModule *>(module)) {
        kcm->load();
    }
}

void KcmHost::openWindow(const QString &pluginId, const QString &title)
{
    const KPluginMetaData data = findModule(pluginId);
    if (!data.isValid()) {
        return;
    }
    auto dialog = new KCMultiDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->addModule(data);
    dialog->setWindowTitle(title);
    dialog->resize(860, 620);
    dialog->show();
}
