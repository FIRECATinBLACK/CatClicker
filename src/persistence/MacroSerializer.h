#pragma once

#include "../macro/Macro.h"

#include <QtCore/QString>

namespace CatClicker {

class MacroSerializer {
public:
    static constexpr qsizetype MaximumFileSize = 64 * 1024 * 1024;
    static constexpr qsizetype MaximumEventCount = 2'000'000;
    static constexpr qsizetype MaximumStringLength = 1024;

    static QByteArray toJson(const Macro &macro);
    static bool fromJson(const QByteArray &data, Macro *macro, QString *error);
    static bool saveToFile(const QString &path, const Macro &macro, QString *error);
    static bool loadFromFile(const QString &path, Macro *macro, QString *error);
};

}
