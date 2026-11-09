#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTabWidget;
class SearchableTable;

// Onglet "Cautions" : cautions deposees (detenues, a restituer/detruire,
// cloturees) et adherents ayant un pret payant sans caution.
class DepositsTab : public QWidget
{
    Q_OBJECT
public:
    explicit DepositsTab(QWidget *parent = nullptr);
    void reload();
    SearchableTable *currentTable() const;

private:
    void fillDeposits();
    void fillMissing();
    void updateButtons();
    QVariantMap meta(SearchableTable *t, int id) const;

    QTabWidget *m_tabs;
    SearchableTable *m_deposits;
    SearchableTable *m_missing;
    QPushButton *m_add;
    QPushButton *m_edit;
    QPushButton *m_close;
    QPushButton *m_delete;
    QPushButton *m_member;
    QLabel *m_total;
};
