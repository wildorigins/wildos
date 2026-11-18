#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Parts with lights that can be recognised without touching them, from
// what any account may read in sysfs. Their lights sit on the I2C bus
// inside the PC, where a wrong write can damage hardware, so Wild OS
// never asks for that bus: these are shown as seen and left alone.
//
// Only parts whose lights are known for certain are listed. Memory cannot
// be told apart without reading the bus, so it is never listed.
namespace Seen
{
struct Light {
    QString name;
    bool white = false;
};

struct Part {
    QString key;
    // The model's own name, as its maker gives it.
    QString name;
    QList<Light> lights;
};

// `root` is /sys, or a copy of its layout for the tests.
QList<Part> graphicsCards(const QString &root);

// The processor's temperature in degrees C from the kernel's own sensor
// driver, or below zero when there is none to read.
double processorTemperature(const QString &root);
}
