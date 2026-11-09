#pragma once

#include "schema.h"

#include <QColor>
#include <QDate>
#include <QList>

// Regles d'entretien du materiel.
//
// Blocs (arrete du 20 novembre 2017 relatif au suivi en service des
// equipements sous pression, en vigueur au 1er janvier 2018) :
//   - inspection periodique : au plus tous les 12 mois ;
//   - requalification : tous les 2 ans en regime standard, tous les 6 ans en
//     regime derogatoire TIV (cahier des charges FFESSM, bouteilles
//     metalliques uniquement : un bloc composite reste en regime standard).
// Detendeurs : revision annuelle (regle du club).
// Gilets : pas d'obligation, verifications enregistrees sans echeance.

// Un entretien : ouvert (en cours, le materiel ne peut pas etre emprunte)
// tant qu'il n'a pas de date de fin.
struct MaintenanceRecord {
    int id = 0;
    QString kind;
    int equipId = 0;
    QString operation;   // cle d'operation (inspection, requalification, revision...) ou "autre"
    QString responsible; // personne responsable (TIV, benevole, prestataire)
    QDate start;
    QDate end;           // invalide tant que l'entretien est en cours
    QString actions;     // actions effectuees
    QString result;      // Conforme, Non conforme
    QVariant cost;       // euros (facultatif)
    QString equipLabel;

    bool open() const { return !end.isValid(); }
    bool conform() const { return !Schema::normalize(result).startsWith(QLatin1String("non")); }
};

namespace Maintenance {

enum class State { None, Ok, Soon, Overdue };

// Fenetre "a prevoir" avant l'echeance
constexpr int SoonDays = 30;

inline const QString Derogatory = QStringLiteral("Dérogatoire (TIV)");
inline const QString Standard = QStringLiteral("Standard");
inline const QString Other = QStringLiteral("autre");
inline const QString Conform = QStringLiteral("Conforme");
inline const QString NonConform = QStringLiteral("Non conforme");

struct Operation {
    QString key;
    QString label;
    QString dateField;   // champ de la fiche mis a jour (derniere date)
    bool mandatory;
};

// Operations proposees pour un type de materiel (la derniere est "Autre intervention").
QList<Operation> operations(const QString &kind);
QString operationLabel(const QString &kind, const QString &key);

struct Due {
    Operation op;
    QDate last;
    QDate next;        // invalide si pas d'echeance
    int months = 0;    // periodicite
    State state = State::None;
    int daysLeft = 0;
};

// Regime effectif d'un bloc (un composite est toujours en regime standard).
QString blocRegime(const QVariantMap &rec);

// Echeances d'un equipement : la derniere date est la plus recente entre
// la fiche (import) et le journal des entretiens.
QList<Due> dues(const EntityDef &def, const QVariantMap &rec, const QList<MaintenanceRecord> &journal,
                const QDate &today = QDate::currentDate());
State worst(const QList<Due> &dues);
// Echeance la plus proche parmi les obligations.
const Due *nextDue(const QList<Due> &dues);

QString stateText(State s);
QColor stateColor(State s);
// "En retard depuis 12 j", "Dans 20 j", "À jour (12/10/2027)"
QString dueText(const Due &d);

} // namespace Maintenance
