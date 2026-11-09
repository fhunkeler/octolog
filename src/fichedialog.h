#pragma once

#include "database.h"

#include <QDialog>

class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
namespace Ui { class Header; }

namespace Fiche {
// Ligne d'etat : disponibilite du materiel ou droits de pret de l'adherent.
// Les liens sont de la forme "kind:id".
QString statusHtml(const EntityDef &def, const QVariantMap &rec, const QList<Loan> &activeLoans);
// Caution d'un adherent : enregistrer, ou modifier / cloturer celle detenue.
void depositAction(QWidget *parent, int memberId);
// Ouvre la fiche detaillee (non modale) d'un adherent ou d'un materiel.
void open(const QString &kind, int id, QWidget *parent);
}

// Fiche detaillee : toutes les caracteristiques, emprunts en cours et historique.
class FicheDialog : public QDialog
{
    Q_OBJECT
public:
    FicheDialog(const EntityDef &def, int id, QWidget *parent = nullptr);
    int recordId() const { return m_id; }
    const EntityDef &def() const { return m_def; }

private:
    void refresh();
    void openOther(QTreeWidgetItem *item);
    void returnSelected();
    void deleteHistoryEntries();

    const EntityDef &m_def;
    int m_id;
    Ui::Header *m_header;
    QScrollArea *m_scroll;
    QGroupBox *m_activeBox;
    QTreeWidget *m_active;
    QPushButton *m_returnLoans;
    QGroupBox *m_historyBox;
    QTreeWidget *m_history;
    QPushButton *m_lend;
    QGroupBox *m_maintBox = nullptr;
    QTreeWidget *m_maint = nullptr;
};
