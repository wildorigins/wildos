#include "sharer.h"

#include <KSystemClipboard>
#include <Purpose/AlternativesModel>
#include <Purpose/Configuration>
#include <Purpose/Job>

#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QMimeDatabase>

Sharer::Sharer(QObject *parent)
    : QObject(parent)
    , m_model(new Purpose::AlternativesModel(this))
{
    m_model->setPluginType(QStringLiteral("Export"));
}

QVariantList Sharer::targets(const QUrl &file)
{
    QVariantList list;
    const QString mime = QMimeDatabase().mimeTypeForUrl(file).name();
    m_model->setInputData(QJsonObject{
        {QStringLiteral("urls"), QJsonArray{file.toString()}},
        {QStringLiteral("mimeType"), mime},
    });
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row);
        // Copying is already done: the toast says so.
        if (index.data(Purpose::AlternativesModel::PluginIdRole).toString() == QLatin1String("clipboardplugin")) {
            continue;
        }
        std::unique_ptr<Purpose::Configuration> config(m_model->configureJob(row));
        if (!config || !config->isReady()) {
            continue;
        }
        list.append(QVariantMap{
            {QStringLiteral("row"), row},
            {QStringLiteral("name"), index.data(Qt::DisplayRole).toString()},
            {QStringLiteral("icon"), index.data(Purpose::AlternativesModel::IconNameRole).toString()},
        });
    }
    return list;
}

void Sharer::share(int row)
{
    auto config = m_model->configureJob(row);
    if (!config || !config->isReady()) {
        delete config;
        Q_EMIT failed(tr("That way of sharing is not available any more. Pick another."));
        return;
    }
    Purpose::Job *job = config->createJob();
    config->setParent(job);
    connect(job, &KJob::result, this, [this, job] {
        if (job->error()) {
            if (job->error() != KJob::KilledJobError) {
                Q_EMIT failed(job->errorString().isEmpty() ? tr("Sharing did not finish.") : job->errorString());
            }
            return;
        }
        const QString link = job->output().value(QStringLiteral("url")).toString();
        if (!link.isEmpty()) {
            auto *mime = new QMimeData;
            mime->setText(link);
            KSystemClipboard::instance()->setMimeData(mime, QClipboard::Clipboard);
            Q_EMIT finished(tr("Link copied. Paste it wherever you want to share it."));
        } else {
            Q_EMIT finished(QString());
        }
    });
    job->start();
}
