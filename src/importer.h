#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Import des classeurs Excel du club. Le type de fichier (adherents, blocs,
// detendeurs, gilets, emprunts) est reconnu automatiquement par ses en-tetes.
namespace Importer {

struct Report {
    QStringList lines;    // resume par fichier
    QStringList removed;  // fiches desactivees (adherents) ou supprimees (materiel) car absentes du fichier
    QStringList warnings; // lignes ignorees, rapprochements impossibles
    bool applied = false;
};

// Importe plusieurs fichiers ; les adherents et le materiel sont traites
// avant les emprunts pour que ceux-ci puissent etre rapproches.
// Pour les adherents et le materiel, le fichier fait foi : les fiches sont
// creees ou mises a jour ; les adherents absents du fichier sont desactives,
// le materiel absent est supprime (sauf emprunt en cours). Tout est fait dans une transaction : si confirm
// retourne faux, rien n'est modifie.
Report importFiles(const QStringList &paths, const std::function<bool(const Report &)> &confirm = {});

} // namespace Importer
