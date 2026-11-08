#pragma once

#include <QString>

namespace kdeck {

// One live reading for a stat key. bar < 0 means the key has no gauge.
struct StatReading {
    QString value = QStringLiteral("—");
    double bar = -1;
    QString sub;
};

StatReading statReading(const QString &id);

} // namespace kdeck
