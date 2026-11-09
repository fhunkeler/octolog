#pragma once

#include "schema.h"

#include <QList>

class QWidget;

// Actions d'emprunt et de retour, accessibles dans les deux sens :
// depuis un adherent (choisir le materiel) ou depuis le materiel
// (choisir l'adherent).
namespace LoanUi {

// Ouvre le choix du materiel a preter a l'adherent. Retourne vrai si un emprunt a ete enregistre.
bool borrowForMember(QWidget *parent, int memberId);

// Ouvre le choix de l'adherent a qui preter le materiel selectionne.
bool lendEquipment(QWidget *parent, const EntityDef &def, const QList<int> &equipIds);

// Enregistre le retour des emprunts indiques (date et remarque demandees).
bool returnLoans(QWidget *parent, const QList<int> &loanIds);

} // namespace LoanUi
