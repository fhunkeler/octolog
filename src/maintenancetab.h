#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTabWidget;
class SearchableTable;

// Onglet "Entretien" : echeances reglementaires, entretiens en cours et journal.
class MaintenanceTab : public QWidget
{
    Q_OBJECT
public:
    explicit MaintenanceTab(QWidget *parent = nullptr);
    void reload();
    SearchableTable *currentTable() const;

private:
    void fillDues();
    void fillOpen();
    void fillJournal();
    void updateButtons();
    // Materiel selectionne dans les echeances : un seul type a la fois.
    bool selectedEquipment(QString *kind, QList<int> *ids);
    void openFiche(SearchableTable *table);

    QTabWidget *m_tabs;
    SearchableTable *m_dues;
    SearchableTable *m_open;
    SearchableTable *m_journal;
    QPushButton *m_start;
    QPushButton *m_record;
    QPushButton *m_finish;
    QPushButton *m_cancel;
    QPushButton *m_delete;
    QPushButton *m_fiche;
    QLabel *m_total;
};
