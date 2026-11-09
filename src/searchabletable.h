#pragma once

#include <QStandardItemModel>
#include <QWidget>
#include <functional>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QTableView;
class SearchProxy;

// Tableau triable avec barre de recherche, choix de colonne, filtre d'etat
// et colonnes masquables (clic droit sur l'en-tete). La disposition des
// colonnes est memorisee sous la cle settingsKey.
class SearchableTable : public QWidget
{
    Q_OBJECT
public:
    explicit SearchableTable(const QString &settingsKey, QWidget *parent = nullptr);
    ~SearchableTable() override;

    QStandardItemModel *model() const { return m_model; }
    QTableView *view() const { return m_view; }
    SearchProxy *proxy() const { return m_proxy; }

    // Remplacement du contenu : conserve la selection et la position.
    void beginReset();
    void setHeaders(const QStringList &headers, const QList<bool> &visibleByDefault = {});
    void appendRow(int id, const QList<QStandardItem *> &items);
    void endReset();

    // Filtres d'etat proposes dans la liste deroulante a droite de la recherche.
    using RowPredicate = std::function<bool(const QStandardItem *first)>;
    void addStatusFilter(const QString &label, RowPredicate predicate);
    void setStatusFilter(int index);
    void setStatusFilter(const QString &label);
    // Masque une entree de la liste des filtres (ex. filtres reserves au mode admin).
    void setStatusFilterVisible(const QString &label, bool visible);

    // Tri initial (avant toute disposition memorisee par l'utilisateur).
    void setDefaultSort(const QString &header, Qt::SortOrder order);
    void setMultiSelection(bool multi);
    QList<int> selectedIds() const;
    int currentId() const;
    bool selectId(int id);
    void selectIds(const QList<int> &ids);
    int visibleCount() const;
    QList<int> visibleIds() const;

    void focusSearch();
    void clearSearch();
    QHBoxLayout *barLayout() const { return m_bar; }
    // Barre de recherche : peut etre placee ailleurs (ex. sur toute la largeur d'un onglet).
    QWidget *searchBar() const { return m_barWidget; }

    // Donnees visibles dans l'ordre affiche, pour l'export.
    QStringList visibleHeaders() const;
    QList<QVariantList> visibleRows() const;

signals:
    void selectionChanged();
    void activated(int id);

private:
    void updateColumnCombo();
    void updateCount();
    void showHeaderMenu(const QPoint &pos);
    void saveState() const;

    QString m_key;
    QStandardItemModel *m_model;
    SearchProxy *m_proxy;
    QTableView *m_view;
    QLineEdit *m_search;
    QComboBox *m_column;
    QComboBox *m_status;
    QLabel *m_count;
    QWidget *m_barWidget;
    QHBoxLayout *m_bar;
    QList<RowPredicate> m_predicates;
    QList<int> m_savedSelection;
    int m_savedCurrent = -1;
    bool m_stateRestored = false;
    QList<bool> m_defaultVisible;
    QString m_sortHeader;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

// Fabrique un element de tableau : texte affiche + valeur de tri.
QStandardItem *makeItem(const QString &text, const QVariant &sortValue = {});
