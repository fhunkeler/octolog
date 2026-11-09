#pragma once

#include <QList>

class QWidget;
struct Deposit;

// Fenetres de gestion des cautions.
namespace DepositUi {

// Enregistre une caution (memberId < 0 : choix de l'adherent dans la fenetre).
// Avec existing : modification d'une caution existante.
bool record(QWidget *parent, int memberId = -1, const Deposit *existing = nullptr);

// Cloture des cautions : restituee, detruite ou encaissee.
bool close(QWidget *parent, const QList<int> &depositIds);

} // namespace DepositUi
