#include "loanstab.h"

#include "database.h"
#include "fichedialog.h"
#include "loandialogs.h"
#include "searchabletable.h"
#include "searchproxy.h"
#include "tablefill.h"
#include "ui.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>

LoansTab::LoansTab(QWidget *parent)
    : QWidget(parent)
    , m_table(new SearchableTable(QStringLiteral("emprunts_en_cours"), this))
{
    m_table->setDefaultSort(QStringLiteral("Date emprunt"), Qt::DescendingOrder);
    m_return = new QPushButton(Ui::icon(QStringLiteral("return")), tr("Enregistrer le re&tour"), this);
    m_member = new QPushButton(Ui::icon(QStringLiteral("fiche")), tr("Fiche de l'&adhérent…"), this);
    m_equipment = new QPushButton(Ui::icon(QStringLiteral("fiche")), tr("Fiche du &matériel…"), this);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_return);
    actions->addStretch();
    actions->addWidget(m_member);
    actions->addWidget(m_equipment);

    m_table->addStatusFilter(tr("Tout matériel"), {});
    for (const EntityDef *def : Schema::equipment()) {
        const QString kind = def->kind;
        m_table->addStatusFilter(def->label, [kind](const QStandardItem *it) {
            return it->data(Roles::Record).toMap().value(QStringLiteral("kind")).toString() == kind;
        });
    }

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(actions);
    layout->addWidget(m_table->searchBar());
    layout->addWidget(Ui::separator(this));
    layout->addWidget(m_table, 1);

    connect(m_return, &QPushButton::clicked, this, [this] { LoanUi::returnLoans(this, m_table->selectedIds()); });
    connect(m_member, &QPushButton::clicked, this, [this] { openFiche(true); });
    connect(m_equipment, &QPushButton::clicked, this, [this] { openFiche(false); });
    connect(m_table, &SearchableTable::selectionChanged, this, &LoansTab::updateButtons);
    // Double-clic : fenetre de retour
    connect(m_table, &SearchableTable::activated, m_return, &QPushButton::click);

    m_table->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->view(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *ret = menu.addAction(m_return->icon(), m_return->text(), m_return, &QPushButton::click);
        ret->setEnabled(m_return->isEnabled());
        menu.setDefaultAction(ret);
        menu.addSeparator();
        menu.addAction(m_member->icon(), m_member->text(), m_member, &QPushButton::click)->setEnabled(m_member->isEnabled());
        menu.addAction(m_equipment->icon(), m_equipment->text(), m_equipment, &QPushButton::click)->setEnabled(m_equipment->isEnabled());
        menu.exec(m_table->view()->viewport()->mapToGlobal(pos));
    });

    connect(&Database::instance(), &Database::changed, this, &LoansTab::reload);
    reload();
}

void LoansTab::reload()
{
    TableFill::loans(m_table, Database::instance().loans(true), TableFill::WithMember | TableFill::WithEquipment);
    updateButtons();
}

QVariantMap LoansTab::currentMeta() const
{
    const int id = m_table->currentId();
    for (int r = 0; r < m_table->model()->rowCount(); ++r) {
        const QStandardItem *it = m_table->model()->item(r, 0);
        if (it->data(Roles::Id).toInt() == id)
            return it->data(Roles::Record).toMap();
    }
    return {};
}

void LoansTab::openFiche(bool member)
{
    const QVariantMap m = currentMeta();
    if (m.isEmpty())
        return;
    if (member)
        Fiche::open(Schema::members().kind, m.value(QStringLiteral("memberId")).toInt(), this);
    else
        Fiche::open(m.value(QStringLiteral("kind")).toString(), m.value(QStringLiteral("equipId")).toInt(), this);
}

void LoansTab::updateButtons()
{
    const int n = m_table->selectedIds().size();
    m_return->setEnabled(n > 0);
    m_member->setEnabled(n == 1);
    m_equipment->setEnabled(n == 1);
}
