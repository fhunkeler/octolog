#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

// Description declarative des entites gerees par OctoLog (adherents et
// materiel). Les tables SQL, les formulaires, les colonnes des vues et
// l'import Excel sont tous generes a partir de ces definitions.

enum class FieldType { Text, LongText, Integer, Real, Date, Choice, Bool };

struct Field {
    QString key;             // nom de colonne SQL
    QString label;           // libelle affiche
    FieldType type = FieldType::Text;
    bool inTable = true;     // colonne visible par defaut dans la liste
    QStringList choices;     // valeurs proposees (Choice)
    QStringList importHeaders; // en-tetes Excel reconnus, par ordre de priorite
};

struct EntityDef {
    QString kind;        // identifiant interne : membres, blocs, detendeurs, gilets
    QString table;       // table SQL
    QString label;       // "Blocs"
    QString singular;    // "bloc"
    bool isEquipment = false;
    QString labelPattern;  // "{nom}" ou "{identifiant} - {taille}"
    QString codeField;     // champ identifiant du materiel
    QString rightField;    // champ adherent donnant le droit d'emprunt
    QList<QStringList> importKeys; // cles de rapprochement lors d'un import
    QList<Field> fields;

    const Field *field(const QString &key) const;
    int fieldIndex(const QString &key) const;
    QString displayLabel(const QVariantMap &rec) const;
};

namespace Schema {
const EntityDef &members();
const QList<const EntityDef *> &equipment();
const EntityDef *byKind(const QString &kind);

// Texte affiche pour une valeur stockee.
QString formatValue(const Field &f, const QVariant &v);
// Valeur typee pour le tri et les comparaisons (QDate, double, QString).
QVariant sortValue(const Field &f, const QVariant &v);
// Conversion d'une saisie ou d'une cellule Excel vers la valeur stockee.
QVariant parseValue(const Field &f, const QString &text);
// "Oui", "Oui (Encadrant)" -> vrai.
bool isYes(const QVariant &v);
// Adherent desactive (absent du dernier import des adherents).
inline bool isInactive(const QVariantMap &member) { return member.value(QStringLiteral("desactive")).toInt() != 0; }
// minuscules, sans accents ni espaces multiples : pour la recherche.
QString normalize(const QString &s);
QDate parseDate(const QString &s);
}
