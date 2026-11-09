#pragma once

#include <QSortFilterProxyModel>
#include <functional>

// Roles des elements de modele utilises dans toute l'application.
namespace Roles {
enum {
    Sort = Qt::UserRole + 1,   // valeur typee (QDate, double, QString)
    Id,                        // id de la fiche (colonne 0)
    Record,                    // QVariantMap de la fiche (colonne 0)
    Status,                    // etat calcule (colonne 0)
    Maintenance,               // etat d'entretien (int Maintenance::State, colonne 0)
};
}

// Filtre de recherche multi-criteres :
//   mots libres       -> chaque mot doit apparaitre dans une colonne
//   "texte exact"     -> expression entre guillemets
//   champ:valeur      -> la colonne dont le titre commence par "champ" contient valeur
//   champ=valeur      -> egalite ; champ= (vide) : colonne vide
//   champ>valeur, <, >=, <=  -> comparaison numerique ou de date (jj/mm/aaaa, aaaa)
//   champ!=valeur     -> different
//   -mot              -> exclut les lignes contenant le mot
// La recherche ignore la casse et les accents.
class SearchProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit SearchProxy(QObject *parent = nullptr);

    void setQuery(const QString &query);
    void setSearchColumn(int column); // -1 : toutes les colonnes
    void setRowFilter(std::function<bool(int sourceRow)> filter);
    void refilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    struct Term {
        int column = -1;
        QString op; // "", ":", "=", "!=", ">", "<", ">=", "<="
        QString text;
        bool negate = false;
    };
    void parse();
    int resolveColumn(const QString &name) const;
    bool matches(const Term &t, int sourceRow) const;
    bool matchesCell(const Term &t, int sourceRow, int column) const;

    QString m_query;
    int m_column = -1;
    QList<Term> m_terms;
    std::function<bool(int)> m_rowFilter;
};
