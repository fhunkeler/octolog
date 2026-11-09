#pragma once

#include "schema.h"

#include <QWidget>

class QFormLayout;
class QAction;
class QFrame;
class QMenu;
class QGroupBox;
class QToolButton;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class SearchableTable;
namespace Ui { class Header; }

// Onglet d'une entite (adherents ou un type de materiel) : liste
// recherchable a gauche, fiche detaillee et emprunts a droite.
class EntityTab : public QWidget
{
    Q_OBJECT
public:
    explicit EntityTab(const EntityDef &def, QWidget *parent = nullptr);

    const EntityDef &def() const { return m_def; }
    SearchableTable *table() const { return m_table; }
    void reload();
    bool showRecord(int id);

signals:
    // Demande d'affichage d'une fiche dans un autre onglet.
    void navigate(const QString &kind, int id);

private:
    void updateDetails();
    void updateButtons();
    void addRecord();
    void editRecord();
    void deleteRecords();
    void lend();
    void giveBack();
    void returnSelectedLoans();
    void loanActivated(QTreeWidgetItem *item);
    void openFiche();
    void finishMaintenance();
    void activateRow();
    void showListMenu(const QPoint &pos);

    const EntityDef &m_def;
    SearchableTable *m_table;

    QPushButton *m_add;
    QPushButton *m_edit;
    QPushButton *m_delete;
    QPushButton *m_lend;
    QPushButton *m_return;

    Ui::Header *m_header;
    QPushButton *m_fiche;
    QPushButton *m_deposit = nullptr;
    QToolButton *m_panelToggle;
    QToolButton *m_maintButton = nullptr;
    QMenu *m_maintMenu = nullptr;
    QAction *m_maintStart = nullptr;
    QAction *m_maintFinish = nullptr;
    QAction *m_maintRecord = nullptr;
    QWidget *m_panel;
    QFrame *m_loansCard;
    QLabel *m_loansTitle;
    QLabel *m_noLoans;
    QTreeWidget *m_active;
    QPushButton *m_returnLoans;
};
