#pragma once

#include <QString>
#include <QStringList>

// Extraction complete des donnees dans un classeur Excel (une feuille par sujet).
namespace Exporter {

// Feuilles : Adherents, Blocs, Detendeurs, Gilets (toutes les caracteristiques,
// disponibilite, entretien), Emprunts en cours, Historique des emprunts,
// Entretiens, Cautions, Echeances. summary recoit une ligne par feuille.
// Sans includeAdmin (mode utilisateur) : ni entretien, ni cautions, ni echeances.
bool exportAll(const QString &path, QString *error, QStringList *summary = nullptr, bool includeAdmin = true);

} // namespace Exporter
