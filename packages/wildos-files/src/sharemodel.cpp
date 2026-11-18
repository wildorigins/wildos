#include "sharemodel.h"

#include <Purpose/AlternativesModel>
#include <Purpose/Configuration>
#include <Purpose/Job>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeDatabase>

Sharer::Sharer(QObject *parent)
    : QObject(parent)
    , m_model(new Purpose::AlternativesModel(this))
{
    m_model->setPluginType(QStringLiteral("Export"));
}

QVariantList Sharer::targets(const QList<QUrl> &urls)
{
    QVariantList list;
    if (urls.isEmpty()) {
        return list;
    }
    QMimeDatabase db;
    QJsonArray array;
    QString common;
    for (const QUrl &url : urls) {
        array.append(url.toString());
        const QString mime = db.mimeTypeForUrl(url).name();
        if (common.isEmpty()) {
            common = mime;
        } else if (common != mime) {
            // Mixed selections share as their common family, or as files.
            common = common.section(QLatin1Char('/'), 0, 0) == mime.section(QLatin1Char('/'), 0, 0)
                ? common.section(QLatin1Char('/'), 0, 0) + QStringLiteral("/*")
                : QStringLiteral("application/octet-stream");
        }
    }
    m_model->setInputData(QJsonObject{{QStringLiteral("urls"), array}, {QStringLiteral("mimeType"), common}});

    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row);
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
        Q_EMIT failed(QCoreApplication::translate("Sharer", "That way of sharing is not available any more. Pick another from the Share menu."));
        return;
    }
    Purpose::Job *job = config->createJob();
    config->setParent(job);
    connect(job, &KJob::result, this, [this, job] {
        if (job->error()) {
            if (job->error() != KJob::KilledJobError) {
                Q_EMIT failed(job->errorString().isEmpty() ? QCoreApplication::translate("Sharer", "Sharing did not finish.") : job->errorString());
            }
            return;
        }
        const QString link = job->output().value(QStringLiteral("url")).toString();
        if (!link.isEmpty()) {
            QGuiApplication::clipboard()->setText(link);
            Q_EMIT finished(QCoreApplication::translate("Sharer", "Link copied. Paste it wherever you want to share it."));
        }
    });
    job->start();
}
