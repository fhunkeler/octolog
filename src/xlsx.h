#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

// Lecture / ecriture minimale de classeurs Excel (.xlsx) : suffisant pour
// importer les exports du site du club et exporter les vues d'OctoLog.
namespace Xlsx {

struct Sheet {
    QString name;
    QList<QStringList> rows; // valeurs brutes (nombres non formates)
};

// Lit toutes les feuilles du classeur, dans l'ordre.
QList<Sheet> read(const QString &path, QString *error);

struct SheetData {
    QString name;
    QStringList headers;
    QList<QVariantList> rows;
};

// Ecrit un classeur de plusieurs feuilles.
bool writeWorkbook(const QString &path, const QList<SheetData> &sheets, QString *error);

// Ecrit une feuille unique. Les QDate et nombres sont ecrits types.
bool write(const QString &path, const QString &sheetName, const QStringList &headers,
           const QList<QVariantList> &rows, QString *error);

} // namespace Xlsx
