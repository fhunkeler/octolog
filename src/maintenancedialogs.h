#pragma once

#include "schema.h"

#include <QList>

class QWidget;

// Fenetres d'entretien du materiel.
namespace MaintenanceUi {

// Demarre un entretien sur le materiel selectionne (il ne pourra plus etre
// emprunte jusqu'a la fin). Avec alreadyDone, enregistre directement un
// entretien deja termine (ex. seance TIV).
bool start(QWidget *parent, const EntityDef &def, const QList<int> &equipIds, bool alreadyDone = false);

// Termine des entretiens en cours : date de fin, actions effectuees, resultat, cout.
bool finish(QWidget *parent, const QList<int> &maintenanceIds);

} // namespace MaintenanceUi
