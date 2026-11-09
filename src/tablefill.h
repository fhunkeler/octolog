#pragma once

#include "database.h"

class SearchableTable;

// Remplissage des tableaux a partir de la base.
namespace TableFill {

// Etat de disponibilite d'un materiel (role Status de la colonne 0).
inline const QString Available = QStringLiteral("Disponible");
inline const QString Borrowed = QStringLiteral("Emprunté");
inline const QString OutOfService = QStringLiteral("Hors service");
inline const QString InMaintenance = QStringLiteral("En entretien");

// Liste d'adherents ou de materiel, avec colonnes calculees
// (emprunts en cours / disponibilite, emprunteur, date).
void entities(SearchableTable *table, const EntityDef &def, const LoanIndex &loans);

// Filtres d'etat standards pour une liste d'adherents ou de materiel.
// Pour les adherents, le filtre "Actifs" est selectionne par defaut.
void addStandardFilters(SearchableTable *table, const EntityDef &def);
inline const QString MembersActive = QStringLiteral("Actifs");
QString rightFilterLabel(const EntityDef &equipment);
// Filtres portant sur des donnees reservees au mode admin (masques en mode utilisateur).
QStringList adminFilters(const EntityDef &def);

enum LoanColumns { WithMember = 1, WithEquipment = 2, WithReturn = 4, WithContact = 8 };
void loans(SearchableTable *table, const QList<Loan> &loans, int columns);

// Texte "depuis n jours" / duree.
QString durationText(int days);

} // namespace TableFill
