#pragma once

#include <QObject>
#include <QStringList>

// Keeps the on-screen keyboard's languages in step with the keyboard
// layouts for as long as the session lasts.
class OskLanguages : public QObject
{
    Q_OBJECT
public:
    explicit OskLanguages(QObject *parent = nullptr);

public Q_SLOTS:
    void sync();
    void follow(uint index);

private:
    QStringList m_layouts;
};
