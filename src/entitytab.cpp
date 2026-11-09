#include "entitytab.h"
#include "admin.h"

#include "database.h"
#include "fichedialog.h"
#include "loandialogs.h"
#include "maintenancedialogs.h"
#include "recorddialog.h"
#include "searchabletable.h"
#include "tablefill.h"
#include "ui.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QTableView>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum { LoanIdRole = Qt::UserRole + 1, MemberRole, KindRole, EquipRole };

QTreeWidget *makeLoanTree(const QStringList &headers, QWidget *parent)
{
    auto *t = new QTreeWidget(parent);
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setAlternatingRowColors(true);
    t->setUniformRowHeights(true);
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->header()->setStretchLastSection(true);
    return t;
}
} // namespace

EntityTab::EntityTab(const EntityDef &def, QWidget *parent)
    : QWidget(parent)
    , m_def(def)
{
    m_table = new SearchableTable(def.kind, this);
    TableFill::addStandardFilters(m_table, def);

    // Barre d'actions
        m_add = new QPushButton(Ui::icon(QStringLiteral("add")), tr("&Nouveau…"), this);
    m_edit = new QPushButton(Ui::icon(QStringLiteral("edit")), tr("&Modifier…"), this);
    m_delete = new QPushButton(Ui::icon(QStringLiteral("delete")), tr("Supprimer"), this);
    if (def.isEquipment) {
        m_lend = new QPushButton(Ui::icon(QStringLiteral("lend")), tr("&Prêter à un adhérent…"), this);
        m_return = new QPushButton(Ui::icon(QStringLiteral("return")), tr("Enregistrer le re&tour"), this);
    } else {
        m_lend = new QPushButton(Ui::icon(QStringLiteral("lend")), tr("&Emprunter du matériel…"), this);
        m_return = new QPushButton(Ui::icon(QStringLiteral("return")), tr("&Tout rendre"), this);
    }
    for (QPushButton *b : {m_add, m_edit, m_delete})
        Admin::adminOnly(b);
    QFont bold = m_lend->font();
    bold.setBold(true);
    m_lend->setFont(bold);
    m_delete->setToolTip(tr("Supprimer les fiches sélectionnées (Suppr)"));

    // Bouton d'affichage du panneau de detail (etat memorise par onglet)
    const QString panelKey = QStringLiteral("tabs/%1/panel").arg(def.kind);
    m_panelToggle = new QToolButton(this);
    m_panelToggle->setIcon(Ui::icon(QStringLiteral("fiche")));
    m_panelToggle->setText(tr("Détails"));
    m_panelToggle->setToolTip(tr("Afficher ou masquer le panneau de détail"));
    m_panelToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_panelToggle->setCheckable(true);
    m_panelToggle->setChecked(QSettings().value(panelKey, true).toBool());

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_add);
    actions->addWidget(m_edit);
    actions->addWidget(m_delete);
    actions->addStretch();
    actions->addWidget(m_lend);
    actions->addWidget(m_return);
    if (def.isEquipment) {
        // Entretien : demarrer, terminer, enregistrer un entretien deja fait
        m_maintMenu = new QMenu(this);
        m_maintStart = m_maintMenu->addAction(Ui::icon(QStringLiteral("maintenance")), tr("Démarrer un entretien…"),
                                              this, [this] { MaintenanceUi::start(this, m_def, m_table->selectedIds()); });
        m_maintFinish = m_maintMenu->addAction(Ui::icon(QStringLiteral("return")), tr("Terminer l'entretien…"), this,
                                               &EntityTab::finishMaintenance);
        m_maintMenu->addSeparator();
        m_maintRecord = m_maintMenu->addAction(tr("Enregistrer un entretien effectué…"), this, [this] {
            MaintenanceUi::start(this, m_def, m_table->selectedIds(), true);
        });
        m_maintButton = new QToolButton(this);
        m_maintButton->setIcon(Ui::icon(QStringLiteral("maintenance")));
        m_maintButton->setText(tr("Entretien"));
        m_maintButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_maintButton->setPopupMode(QToolButton::InstantPopup);
        m_maintButton->setMenu(m_maintMenu);
        Admin::adminOnly(m_maintButton);
        actions->addWidget(m_maintButton);
    }
    actions->addWidget(separator);
    actions->addWidget(m_panelToggle);

    // Panneau de detail : carte d'en-tete + carte des emprunts en cours
    m_header = new Ui::Header(this);
    connect(m_header->details(), &QLabel::linkActivated, this, [this](const QString &link) {
        const QStringList parts = link.split(QLatin1Char(':'));
        if (parts.size() == 2)
            Fiche::open(parts[0], parts[1].toInt(), this);
    });
    m_fiche = new QPushButton(Ui::icon(QStringLiteral("fiche")), tr("&Fiche détaillée…"), this);
    m_fiche->setToolTip(tr("Toutes les caractéristiques et l'historique des emprunts"));
    m_header->addButton(m_fiche);
    if (!def.isEquipment) {
        m_deposit = new QPushButton(Ui::icon(QStringLiteral("deposit")), tr("&Caution…"), this);
        m_deposit->setToolTip(tr("Enregistrer, modifier ou clôturer la caution (prêts payants)"));
        m_header->addButton(m_deposit);
        connect(m_deposit, &QPushButton::clicked, this, [this] {
            if (m_table->currentId() >= 0)
                Fiche::depositAction(this, m_table->currentId());
        });
    }

    m_loansCard = Ui::card(this);
    m_loansTitle = new QLabel(m_loansCard);
    m_active = makeLoanTree({def.isEquipment ? tr("Adhérent") : tr("Matériel"), tr("Depuis le"), tr("Durée"),
                             tr("Remarque")}, m_loansCard);
    m_active->setFrameShape(QFrame::NoFrame);
    m_active->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_active->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_loansCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_loansTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_active->setToolTip(tr("Double-clic : ouvrir la fiche · clic droit : rendre"));
    m_noLoans = new QLabel(def.isEquipment ? tr("Aucun emprunt en cours : ce matériel est au local.")
                                           : tr("Aucun matériel emprunté."), m_loansCard);
    m_noLoans->setWordWrap(true);
    QPalette muted = m_noLoans->palette();
    muted.setColor(QPalette::WindowText, muted.color(QPalette::Disabled, QPalette::WindowText));
    m_noLoans->setPalette(muted);
    m_returnLoans = new QPushButton(Ui::icon(QStringLiteral("return")), tr("Rendre la sélection"), m_loansCard);
    auto *loansLayout = new QVBoxLayout(m_loansCard);
    loansLayout->setContentsMargins(14, 12, 14, 12);
    loansLayout->addWidget(m_loansTitle);
    loansLayout->addWidget(m_active);
    loansLayout->addWidget(m_noLoans);
    auto *loansButtons = new QHBoxLayout;
    loansButtons->addStretch();
    loansButtons->addWidget(m_returnLoans);
    loansLayout->addLayout(loansButtons);

    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Masquer le panneau de détail"));
    auto *panelTitle = new QLabel(tr("Détails"), this);
    panelTitle->setPalette(muted);
    auto *panelTop = new QHBoxLayout;
    panelTop->addWidget(panelTitle);
    panelTop->addStretch();
    panelTop->addWidget(close);

    m_panel = new QWidget(this);
    auto *panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(6, 0, 0, 0);
    panelLayout->setSpacing(8);
    panelLayout->addLayout(panelTop);
    panelLayout->addWidget(m_header);
    panelLayout->addWidget(m_loansCard);
    panelLayout->addStretch(1);
    m_panel->setVisible(m_panelToggle->isChecked());

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_table);
    split->addWidget(m_panel);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setChildrenCollapsible(false);
    const QString splitKey = QStringLiteral("tabs/%1/split2").arg(def.kind);
    if (!split->restoreState(QSettings().value(splitKey).toByteArray()))
        split->setSizes({900, 380});
    connect(split, &QSplitter::splitterMoved, this, [split, splitKey] {
        QSettings().setValue(splitKey, split->saveState());
    });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(actions);
    layout->addWidget(m_table->searchBar());
    layout->addWidget(Ui::separator(this));
    layout->addWidget(split, 1);

    connect(m_panelToggle, &QToolButton::toggled, this, [this, panelKey](bool on) {
        m_panel->setVisible(on);
        QSettings().setValue(panelKey, on);
    });
    connect(close, &QToolButton::clicked, m_panelToggle, [this] { m_panelToggle->setChecked(false); });

    connect(m_add, &QPushButton::clicked, this, &EntityTab::addRecord);
    connect(m_edit, &QPushButton::clicked, this, &EntityTab::editRecord);
    connect(m_delete, &QPushButton::clicked, this, &EntityTab::deleteRecords);
    connect(m_lend, &QPushButton::clicked, this, &EntityTab::lend);
    connect(m_return, &QPushButton::clicked, this, &EntityTab::giveBack);
    connect(m_returnLoans, &QPushButton::clicked, this, &EntityTab::returnSelectedLoans);
    connect(m_table, &SearchableTable::selectionChanged, this, [this] {
        updateDetails();
        updateButtons();
    });
    connect(m_table, &SearchableTable::activated, this, &EntityTab::activateRow);
    connect(m_fiche, &QPushButton::clicked, this, &EntityTab::openFiche);
    connect(m_active, &QTreeWidget::itemDoubleClicked, this, &EntityTab::loanActivated);
    connect(m_active, &QTreeWidget::itemSelectionChanged, this, &EntityTab::updateButtons);

    // Menu contextuel de la liste
    m_table->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->view(), &QWidget::customContextMenuRequested, this, &EntityTab::showListMenu);
    m_active->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_active, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_active->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        QAction *ret = menu.addAction(Ui::icon(QStringLiteral("return")),
                                      m_active->selectedItems().size() > 1 ? tr("Rendre la sélection")
                                                                           : tr("Rendre « %1 »").arg(item->text(0)),
                                      this, &EntityTab::returnSelectedLoans);
        menu.setDefaultAction(ret);
        menu.addAction(Ui::icon(QStringLiteral("fiche")),
                       m_def.isEquipment ? tr("Fiche de l'adhérent…") : tr("Fiche du matériel…"), this,
                       [this, item] { loanActivated(item); });
        menu.exec(m_active->viewport()->mapToGlobal(pos));
    });
    auto *del = new QAction(this);
    del->setShortcut(QKeySequence::Delete);
    del->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(del, &QAction::triggered, this, &EntityTab::deleteRecords);
    m_table->view()->addAction(del);

    connect(&Database::instance(), &Database::changed, this, &EntityTab::reload);
    connect(&Admin::instance(), &Admin::changed, this, &EntityTab::reload);
    reload();
}

void EntityTab::reload()
{
    for (const QString &f : TableFill::adminFilters(m_def))
        m_table->setStatusFilterVisible(f, Admin::instance().isAdmin());
    TableFill::entities(m_table, m_def, Database::instance().activeLoanIndex());
    updateDetails();
    updateButtons();
}

bool EntityTab::showRecord(int id)
{
    return m_table->selectId(id);
}

void EntityTab::updateButtons()
{
    const QList<int> ids = m_table->selectedIds();
    m_edit->setEnabled(ids.size() == 1);
    m_fiche->setEnabled(ids.size() == 1);
    m_delete->setEnabled(!ids.isEmpty());
    if (m_def.isEquipment) {
        m_lend->setEnabled(!ids.isEmpty());
        const LoanIndex idx = Database::instance().activeLoanIndex();
        bool anyBorrowed = false;
        for (int id : ids)
            anyBorrowed |= idx.byEquipment.value(m_def.kind).contains(id);
        m_return->setEnabled(anyBorrowed);
        bool anyOpen = false, anyFree = false;
        for (int id : ids) {
            const bool open = idx.inMaintenance.value(m_def.kind).contains(id);
            anyOpen |= open;
            anyFree |= !open && !idx.byEquipment.value(m_def.kind).contains(id);
        }
        m_maintStart->setEnabled(anyFree);
        m_maintFinish->setEnabled(anyOpen);
        m_maintRecord->setEnabled(anyFree);
        m_maintButton->setEnabled(!ids.isEmpty());
    } else {
        m_lend->setEnabled(ids.size() == 1);
        m_return->setEnabled(ids.size() == 1 && m_active->topLevelItemCount() > 0);
    }
    m_returnLoans->setEnabled(!m_active->selectedItems().isEmpty());
}

void EntityTab::updateDetails()
{
    m_active->clear();
    const int id = m_table->selectedIds().size() == 1 ? m_table->currentId() : -1;
    if (id < 0) {
        const int n = m_table->selectedIds().size();
        m_header->setEmpty(m_def.kind, n > 1 ? tr("%1 éléments sélectionnés").arg(n) : tr("Aucune sélection"));
        m_header->setDetails(n > 1 ? tr("Utilisez les boutons ou le clic droit pour agir sur toute la sélection.")
                                   : tr("Cliquez sur une ligne pour afficher son état et ses emprunts."));
        m_fiche->hide();
        if (m_deposit)
            m_deposit->hide();
        m_loansCard->hide();
        return;
    }
    Database &db = Database::instance();
    const QVariantMap rec = db.record(m_def, id);

    const QList<Loan> active = m_def.isEquipment ? db.loans(true, -1, m_def.kind, id) : db.loans(true, id);
    for (const Loan &l : active) {
        auto *item = new QTreeWidgetItem;
        item->setData(0, LoanIdRole, l.id);
        item->setData(0, MemberRole, l.memberId);
        item->setData(0, KindRole, l.kind);
        item->setData(0, EquipRole, l.equipId);
        item->setText(0, m_def.isEquipment ? l.memberLabel : l.equipLabel);
        item->setText(1, l.out.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
        item->setText(2, TableFill::durationText(l.durationDays()));
        item->setText(3, l.notesOut);
        m_active->addTopLevelItem(item);
    }
    for (int c = 0; c < m_active->columnCount() - 1; ++c)
        m_active->resizeColumnToContents(c);
    m_header->setRecord(m_def, rec, active.size(),
                        m_def.isEquipment && !db.maintenances(m_def.kind, id, true).isEmpty());
    m_header->setDetails(Fiche::statusHtml(m_def, rec, active));
    m_fiche->show();
    if (m_deposit)
        m_deposit->setVisible(Admin::instance().isAdmin() && (Deposits::needsDeposit(rec) || !db.deposits(id).isEmpty()));
    m_loansCard->show();
    m_loansTitle->setText(QStringLiteral("<img src=\":/icons/loans.svg\" width=\"18\" height=\"18\" style=\"vertical-align:middle\">"
                                         "&nbsp;&nbsp;<b>%1</b>")
                              .arg(m_def.isEquipment ? tr("Emprunt en cours")
                                                     : tr("Matériel emprunté (%1)").arg(active.size())));
    const bool any = !active.isEmpty();
    m_active->setVisible(any);
    m_returnLoans->setVisible(any);
    m_noLoans->setVisible(!any);
    if (any) {
        // Hauteur ajustee au nombre de lignes (le panneau ne s'etire pas a vide)
        const int rowH = fontMetrics().height() + 10;
        m_active->setFixedHeight(qMin(320, m_active->header()->sizeHint().height() + rowH * int(active.size()) + 4));
    }
    // Recalcul immediat : la carte epouse son contenu
    m_loansCard->layout()->activate();
    m_loansCard->setFixedHeight(m_loansCard->layout()->sizeHint().height());
}

void EntityTab::openFiche()
{
    if (m_table->selectedIds().size() == 1)
        Fiche::open(m_def.kind, m_table->currentId(), this);
}

void EntityTab::addRecord()
{
    if (!Admin::require(this))
        return;
    RecordDialog dlg(m_def, {}, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString err;
    const int id = Database::instance().insert(m_def, dlg.values(), &err);
    if (id < 0)
        QMessageBox::warning(this, tr("Création"), err);
    else
        m_table->selectId(id);
}

void EntityTab::editRecord()
{
    if (!Admin::require(this))
        return;
    const int id = m_table->currentId();
    if (id < 0)
        return;
    RecordDialog dlg(m_def, Database::instance().record(m_def, id), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString err;
    if (!Database::instance().update(m_def, id, dlg.values(), &err))
        QMessageBox::warning(this, tr("Modification"), err);
}

void EntityTab::deleteRecords()
{
    if (!Admin::require(this))
        return;
    const QList<int> ids = m_table->selectedIds();
    if (ids.isEmpty())
        return;
    Database &db = Database::instance();
    QStringList labels;
    for (int id : ids)
        labels << db.label(m_def.kind, id);
    const QString question = ids.size() == 1
        ? tr("Supprimer définitivement « %1 » ?\nL'historique de ses emprunts est conservé.").arg(labels.first())
        : tr("Supprimer définitivement ces %1 fiches ?\n%2\n\nL'historique de leurs emprunts est conservé.")
              .arg(ids.size()).arg(labels.mid(0, 15).join(QStringLiteral(", ")));
    if (QMessageBox::question(this, tr("Suppression"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    QStringList errors;
    db.beginBatch();
    for (int id : ids) {
        QString err;
        if (!db.remove(m_def, id, &err))
            errors << err;
    }
    db.endBatch();
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Suppression"), errors.join('\n'));
}

void EntityTab::finishMaintenance()
{
    const LoanIndex idx = Database::instance().activeLoanIndex();
    QList<int> ids;
    for (int id : m_table->selectedIds())
        if (idx.inMaintenance.value(m_def.kind).contains(id))
            ids << idx.inMaintenance.value(m_def.kind).value(id).id;
    MaintenanceUi::finish(this, ids);
}

void EntityTab::activateRow()
{
    // Double-clic : fenetre d'emprunt (ou de retour pour un materiel deja emprunte).
    if (m_table->selectedIds().size() != 1)
        return;
    if (m_def.isEquipment && m_return->isEnabled())
        giveBack();
    else if (m_def.isEquipment && m_maintFinish->isEnabled())
        Admin::instance().isAdmin() ? finishMaintenance() : openFiche();
    else
        lend();
}

void EntityTab::showListMenu(const QPoint &pos)
{
    if (!m_table->view()->indexAt(pos).isValid()) {
        if (!Admin::instance().isAdmin())
            return;
        QMenu menu(this);
        menu.addAction(m_add->icon(), m_add->text(), this, &EntityTab::addRecord);
        menu.exec(m_table->view()->viewport()->mapToGlobal(pos));
        return;
    }
        const QList<int> ids = m_table->selectedIds();
    const int id = ids.size() == 1 ? m_table->currentId() : -1;
    QMenu menu(this);

    if (m_def.isEquipment) {
        const bool borrowed = m_return->isEnabled();
        QAction *lendAction = menu.addAction(m_lend->icon(), m_lend->text(), this, &EntityTab::lend);
        QAction *returnAction = menu.addAction(m_return->icon(), m_return->text(), this, &EntityTab::giveBack);
        returnAction->setEnabled(borrowed);
        menu.setDefaultAction(id >= 0 && borrowed ? returnAction : lendAction);
        if (m_maintFinish->isEnabled()) {
            lendAction->setEnabled(false);
            menu.setDefaultAction(menu.addAction(m_maintFinish->icon(), m_maintFinish->text(), this,
                                                 &EntityTab::finishMaintenance));
        }
        if (Admin::instance().isAdmin()) {
            QMenu *maint = menu.addMenu(Ui::icon(QStringLiteral("maintenance")), tr("Entretien"));
            maint->addActions(m_maintMenu->actions());
        }
        if (id >= 0 && borrowed) {
            const Loan l = Database::instance().activeLoanIndex().byEquipment.value(m_def.kind).value(id);
            menu.addAction(Ui::icon(QStringLiteral("fiche")),
                           tr("Fiche de l'emprunteur (%1)…").arg(l.memberLabel), this,
                           [this, l] { Fiche::open(Schema::members().kind, l.memberId, this); });
        }
    } else {
        QAction *lendAction = menu.addAction(m_lend->icon(), m_lend->text(), this, &EntityTab::lend);
        lendAction->setEnabled(id >= 0);
        menu.setDefaultAction(lendAction);
        const QList<Loan> loans = id >= 0 ? Database::instance().loans(true, id) : QList<Loan>();
        if (!loans.isEmpty()) {
            QMenu *ret = menu.addMenu(Ui::icon(QStringLiteral("return")), tr("Rendre"));
            for (const Loan &l : loans)
                ret->addAction(l.equipLabel, this, [this, l] { LoanUi::returnLoans(this, {l.id}); });
            ret->addSeparator();
            ret->addAction(tr("Tout rendre (%1)").arg(loans.size()), this, &EntityTab::giveBack);
            QMenu *open = menu.addMenu(Ui::icon(QStringLiteral("fiche")), tr("Fiche du matériel emprunté"));
            for (const Loan &l : loans)
                open->addAction(l.equipLabel, this, [this, l] { Fiche::open(l.kind, l.equipId, this); });
        }
    }
    if (!m_def.isEquipment && id >= 0 && Admin::instance().isAdmin())
        menu.addAction(Ui::icon(QStringLiteral("deposit")), tr("Caution…"), this,
                       [this, id] { Fiche::depositAction(this, id); });
    menu.addSeparator();
    menu.addAction(m_fiche->icon(), m_fiche->text(), this, &EntityTab::openFiche)->setEnabled(id >= 0);
    if (Admin::instance().isAdmin()) {
        menu.addAction(m_edit->icon(), m_edit->text(), this, &EntityTab::editRecord)->setEnabled(id >= 0);
        menu.addSeparator();
        menu.addAction(m_add->icon(), m_add->text(), this, &EntityTab::addRecord);
        menu.addAction(m_delete->icon(), ids.size() > 1 ? tr("Supprimer les %1 fiches").arg(ids.size()) : m_delete->text(),
                       this, &EntityTab::deleteRecords);
    }
    menu.exec(m_table->view()->viewport()->mapToGlobal(pos));
}

void EntityTab::lend()
{
    if (m_def.isEquipment)
        LoanUi::lendEquipment(this, m_def, m_table->selectedIds());
    else if (m_table->currentId() >= 0)
        LoanUi::borrowForMember(this, m_table->currentId());
}

void EntityTab::giveBack()
{
    QList<int> loanIds;
    if (m_def.isEquipment) {
        const LoanIndex idx = Database::instance().activeLoanIndex();
        for (int id : m_table->selectedIds())
            if (idx.byEquipment.value(m_def.kind).contains(id))
                loanIds << idx.byEquipment.value(m_def.kind).value(id).id;
    } else {
        for (int i = 0; i < m_active->topLevelItemCount(); ++i)
            loanIds << m_active->topLevelItem(i)->data(0, LoanIdRole).toInt();
    }
    LoanUi::returnLoans(this, loanIds);
}

void EntityTab::returnSelectedLoans()
{
    QList<int> loanIds;
    for (QTreeWidgetItem *it : m_active->selectedItems())
        loanIds << it->data(0, LoanIdRole).toInt();
    LoanUi::returnLoans(this, loanIds);
}

void EntityTab::loanActivated(QTreeWidgetItem *item)
{
    if (m_def.isEquipment)
        Fiche::open(Schema::members().kind, item->data(0, MemberRole).toInt(), this);
    else
        Fiche::open(item->data(0, KindRole).toString(), item->data(0, EquipRole).toInt(), this);
}
