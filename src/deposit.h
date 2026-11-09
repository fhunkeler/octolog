#pragma once

#include "schema.h"

#include <QDate>
#include <QList>
#include <QVariant>

// Caution deposee par un adherent ayant un pret payant (une caution par saison,
// qui couvre tous ses prets payants). Elle est "detenue" tant qu'elle n'est pas
// cloturee (restituee, detruite ou encaissee).
struct Deposit {
    int id = 0;
    int memberId = 0;
    QString season;      // "2025-2026"
    double amount = 0;
    QString method;      // Chèque, Espèces, Virement
    QString number;      // n° de cheque
    QString bank;
    QDate received;
    QString status;      // Reçue, Restituée, Détruite, Encaissée
    QDate closed;
    QString note;
    QString memberLabel;

    bool held() const { return !closed.isValid(); }
};

namespace Deposits {

inline const QString Received = QStringLiteral("Reçue");
inline const QString Returned = QStringLiteral("Restituée");
inline const QString Destroyed = QStringLiteral("Détruite");
inline const QString Cashed = QStringLiteral("Encaissée");

QStringList methods();          // Chèque, Espèces, Virement
QStringList closingStatuses();  // Restituée, Détruite, Encaissée

// Saison sportive (septembre -> aout) d'une date : "2025-2026".
QString season(const QDate &date);

// Types de materiel dont le pret est payant pour cet adherent : droit de pret
// "Oui" (un "Oui (Encadrant)" est gratuit et ne demande pas de caution).
QList<const EntityDef *> paidTypes(const QVariantMap &member);
inline bool needsDeposit(const QVariantMap &member) { return !paidTypes(member).isEmpty(); }

} // namespace Deposits
