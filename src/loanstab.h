#pragma once

#include <QWidget>

class QPushButton;
class SearchableTable;

// Onglet "Emprunts en cours". L'historique n'est consultable que dans les fiches detaillees.
class LoansTab : public QWidget
{
    Q_OBJECT
public:
    explicit LoansTab(QWidget *parent = nullptr);

    SearchableTable *table() const { return m_table; }
    void reload();

private:
    void updateButtons();
    QVariantMap currentMeta() const;
    void openFiche(bool member);

    SearchableTable *m_table;
    QPushButton *m_return;
    QPushButton *m_member;
    QPushButton *m_equipment;
};
