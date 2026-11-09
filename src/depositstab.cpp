#include "depositstab.h"
#include "admin.h"

#include "charts.h"
#include "database.h"
#include "depositdialogs.h"
#include "fichedialog.h"
#include "searchabletable.h"
#include "searchproxy.h"
#include "ui.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace {
// Etats calcules d'une caution (role Status)
const QString Held = QStringLiteral("held");
const QString ToClose = QStringLiteral("toclose");
const QString Closed = QStringLiteral("closed");

QString money(double v)
{
    return QLocale().toCurrencyString(v, QStringLiteral("€"));
}
} // namespace

DepositsTab::DepositsTab(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabWidget(this))
    , m_deposits(new SearchableTable(QStringLiteral("cautions"), this))
    , m_missing(new SearchableTable(QStringLiteral("cautions_manquantes"), this))
    , m_total(new QLabel(this))
{
    m_add = new QPushButton(Ui::icon(QStringLiteral("deposit")), tr("&Enregistrer une caution…"), this);
    m_edit = new QPushButton(Ui::icon(QStringLiteral("edit")), tr("&Modifier…"), this);
    m_close = new QPushButton(Ui::icon(QStringLiteral("return")), tr("&Clôturer (restituée, détruite…)"), this);
    m_delete = new QPushButton(Ui::icon(QStringLiteral("delete")), tr("Supprimer"), this);
    m_member = new QPushButton(Ui::icon(QStringLiteral("fiche")), tr("Fiche de l'&adhérent…"), this);
    QFont bold = m_add->font();
    bold.setBold(true);
    m_add->setFont(bold);
    m_close->setToolTip(tr("Caution rendue à l'adhérent, chèque détruit ou encaissé"));
    m_delete->setToolTip(tr("Supprime une caution saisie par erreur"));

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_add);
    actions->addWidget(m_edit);
    actions->addWidget(m_close);
    actions->addWidget(m_delete);
    actions->addStretch();
    actions->addWidget(m_member);

    // Cautions
    m_deposits->setDefaultSort(tr("Reçue le"), Qt::DescendingOrder);
    m_deposits->addStatusFilter(tr("Détenues"), [](const QStandardItem *it) {
        return it->data(Roles::Status).toString() != Closed;
    });
    m_deposits->addStatusFilter(tr("À restituer / détruire (plus de prêt en cours)"), [](const QStandardItem *it) {
        return it->data(Roles::Status).toString() == ToClose;
    });
    m_deposits->addStatusFilter(tr("Clôturées"), [](const QStandardItem *it) {
        return it->data(Roles::Status).toString() == Closed;
    });
    m_deposits->addStatusFilter(tr("Toutes"), {});

    auto *depositsPage = new QWidget(this);
    auto *dl = new QVBoxLayout(depositsPage);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->addWidget(m_deposits, 1);
    dl->addWidget(m_total, 0, Qt::AlignRight);

    m_tabs->setIconSize(QSize(18, 18));
    m_tabs->addTab(depositsPage, Ui::icon(QStringLiteral("deposit")), tr("Cautions"));
    m_tabs->addTab(m_missing, Ui::icon(QStringLiteral("warning")), tr("Cautions manquantes"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(actions);
    layout->addWidget(Ui::separator(this));
    layout->addWidget(m_tabs, 1);

    connect(m_tabs, &QTabWidget::currentChanged, this, &DepositsTab::updateButtons);
    for (SearchableTable *t : {m_deposits, m_missing})
        connect(t, &SearchableTable::selectionChanged, this, &DepositsTab::updateButtons);
    connect(m_deposits->proxy(), &QAbstractItemModel::layoutChanged, this, &DepositsTab::updateButtons);
    connect(m_deposits->proxy(), &QAbstractItemModel::modelReset, this, &DepositsTab::updateButtons);
    connect(m_deposits->proxy(), &QAbstractItemModel::rowsInserted, this, &DepositsTab::updateButtons);
    connect(m_deposits->proxy(), &QAbstractItemModel::rowsRemoved, this, &DepositsTab::updateButtons);

    connect(m_add, &QPushButton::clicked, this, [this] {
        int memberId = -1;
        if (m_tabs->currentIndex() == 1 && m_missing->currentId() > 0)
            memberId = m_missing->currentId();
        DepositUi::record(this, memberId);
    });
    connect(m_edit, &QPushButton::clicked, this, [this] {
        const int id = m_deposits->currentId();
        for (const Deposit &d : Database::instance().deposits())
            if (d.id == id)
                DepositUi::record(this, d.memberId, &d);
    });
    connect(m_close, &QPushButton::clicked, this, [this] { DepositUi::close(this, m_deposits->selectedIds()); });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const QList<int> ids = m_deposits->selectedIds();
        if (!Admin::require(this))
            return;
        if (ids.isEmpty()
            || QMessageBox::question(this, tr("Supprimer"),
                                     tr("Supprimer définitivement %n caution(s) ?\nÀ utiliser uniquement pour corriger une erreur de saisie.",
                                        nullptr, ids.size()),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                   != QMessageBox::Yes)
            return;
        Database::instance().beginBatch();
        for (int id : ids)
            Database::instance().removeDeposit(id, nullptr);
        Database::instance().endBatch();
    });
    connect(m_member, &QPushButton::clicked, this, [this] {
        const int memberId = m_tabs->currentIndex() == 1
                                 ? m_missing->currentId()
                                 : meta(m_deposits, m_deposits->currentId()).value(QStringLiteral("memberId")).toInt();
        if (memberId > 0)
            Fiche::open(Schema::members().kind, memberId, this);
    });
    // Double-clic : action principale
    connect(m_deposits, &SearchableTable::activated, this, [this] {
        if (m_close->isVisible() && m_close->isEnabled())
            m_close->click();
        else
            m_member->click();
    });
    connect(m_missing, &SearchableTable::activated, this, [this] {
        (Admin::instance().isAdmin() ? m_add : m_member)->click();
    });

    for (SearchableTable *t : {m_deposits, m_missing}) {
        t->view()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(t->view(), &QWidget::customContextMenuRequested, this, [this, t](const QPoint &pos) {
            QMenu menu(this);
            for (QPushButton *b : {m_add, m_close, m_edit, m_member, m_delete})
                if (b->isVisible())
                    menu.addAction(b->icon(), b->text(), b, &QPushButton::click)->setEnabled(b->isEnabled());
            if (!menu.actions().isEmpty())
                menu.setDefaultAction(menu.actions().first());
            menu.exec(t->view()->viewport()->mapToGlobal(pos));
        });
    }

    connect(&Database::instance(), &Database::changed, this, &DepositsTab::reload);
    connect(&Admin::instance(), &Admin::changed, this, &DepositsTab::updateButtons);
    reload();
}

SearchableTable *DepositsTab::currentTable() const
{
    return m_tabs->currentIndex() == 1 ? m_missing : m_deposits;
}

QVariantMap DepositsTab::meta(SearchableTable *t, int id) const
{
    for (int r = 0; r < t->model()->rowCount(); ++r) {
        const QStandardItem *it = t->model()->item(r, 0);
        if (it->data(Roles::Id).toInt() == id)
            return it->data(Roles::Record).toMap();
    }
    return {};
}

void DepositsTab::reload()
{
    fillDeposits();
    fillMissing();
    updateButtons();
}

void DepositsTab::fillDeposits()
{
    Database &db = Database::instance();
    const LoanIndex idx = db.activeLoanIndex();
    m_deposits->beginReset();
    m_deposits->setHeaders({tr("Adhérent"), tr("Saison"), tr("Montant"), tr("Moyen"), tr("N° chèque"), tr("Banque"),
                            tr("Reçue le"), tr("Statut"), tr("Clôturée le"), tr("Emprunts en cours"), tr("Remarque")});
    int toClose = 0;
    for (const Deposit &d : db.deposits()) {
        const int loans = int(idx.byMember.value(d.memberId).size());
        const QString state = !d.held() ? Closed : loans == 0 ? ToClose : Held;
        QList<QStandardItem *> items;
        items << makeItem(d.memberLabel) << makeItem(d.season) << makeItem(money(d.amount), d.amount) << makeItem(d.method)
              << makeItem(d.number) << makeItem(d.bank)
              << makeItem(d.received.toString(QStringLiteral("dd/MM/yyyy")), d.received);
        QStandardItem *status = makeItem(d.held() && loans == 0 ? tr("Reçue · à restituer / détruire") : d.status);
        status->setIcon(Ui::dot(!d.held() ? Charts::outOfService() : loans == 0 ? Charts::statusWarning() : Charts::statusGood()));
        items << status;
        items << makeItem(d.closed.isValid() ? d.closed.toString(QStringLiteral("dd/MM/yyyy")) : QString(), d.closed)
              << makeItem(loans ? QString::number(loans) : QString(), loans) << makeItem(d.note);
        if (!d.held())
            for (QStandardItem *it : items)
                it->setForeground(QPalette().color(QPalette::Disabled, QPalette::Text));
        items.first()->setData(QVariantMap{{"memberId", d.memberId}}, Roles::Record);
        items.first()->setData(state, Roles::Status);
        m_deposits->appendRow(d.id, items);
        toClose += state == ToClose;
    }
    m_deposits->endReset();
    m_tabs->setTabText(0, toClose ? tr("Cautions (%1 à restituer / détruire)").arg(toClose) : tr("Cautions"));
}

void DepositsTab::fillMissing()
{
    Database &db = Database::instance();
    const LoanIndex idx = db.activeLoanIndex();
    const EntityDef &mdef = Schema::members();
    m_missing->beginReset();
    m_missing->setHeaders({tr("Adhérent"), tr("Prêts payants"), tr("Matériel emprunté"), tr("Téléphone"), tr("Email")});
    int count = 0;
    for (const QVariantMap &rec : db.records(mdef)) {
        const int id = rec.value(QStringLiteral("id")).toInt();
        if (Schema::isInactive(rec) || !Deposits::needsDeposit(rec) || idx.heldDeposit.contains(id))
            continue;
        QStringList types, borrowed;
        for (const EntityDef *e : Deposits::paidTypes(rec))
            types << e->singular;
        for (const Loan &l : idx.byMember.value(id))
            borrowed << l.equipLabel;
        QList<QStandardItem *> items;
        items << makeItem(mdef.displayLabel(rec)) << makeItem(types.join(QStringLiteral(", ")))
              << makeItem(borrowed.join(QStringLiteral(", "))) << makeItem(rec.value(QStringLiteral("telephone")).toString())
              << makeItem(rec.value(QStringLiteral("email")).toString());
        items.first()->setIcon(Ui::dot(borrowed.isEmpty() ? Charts::statusWarning() : Charts::statusCritical()));
        items.first()->setToolTip(borrowed.isEmpty() ? tr("Pas de caution (aucun matériel emprunté pour l'instant)")
                                                     : tr("Matériel emprunté sans caution"));
        m_missing->appendRow(id, items);
        ++count;
    }
    m_missing->endReset();
    m_tabs->setTabText(1, count ? tr("Cautions manquantes (%1)").arg(count) : tr("Cautions manquantes"));
}

void DepositsTab::updateButtons()
{
    const bool depositsPage = m_tabs->currentIndex() == 0;
    const bool admin = Admin::instance().isAdmin();
    const int n = currentTable()->selectedIds().size();
    m_add->setVisible(admin);
    m_edit->setVisible(admin && depositsPage);
    m_close->setVisible(admin && depositsPage);
    m_delete->setVisible(admin && depositsPage);
    m_total->setVisible(depositsPage);
    bool anyHeld = false;
    for (int id : m_deposits->selectedIds())
        for (int r = 0; r < m_deposits->model()->rowCount(); ++r) {
            const QStandardItem *it = m_deposits->model()->item(r, 0);
            if (it->data(Roles::Id).toInt() == id && it->data(Roles::Status).toString() != Closed)
                anyHeld = true;
        }
    m_edit->setEnabled(depositsPage && n == 1);
    m_close->setEnabled(depositsPage && anyHeld);
    m_delete->setEnabled(depositsPage && n > 0);
    m_member->setEnabled(n == 1);

    // Total des cautions affichees
    double total = 0;
    for (int r = 0; r < m_deposits->proxy()->rowCount(); ++r)
        total += m_deposits->proxy()->index(r, 2).data(Roles::Sort).toDouble();
    m_total->setText(tr("Total des cautions affichées : <b>%1</b> (%n caution(s))", nullptr, m_deposits->proxy()->rowCount())
                         .arg(money(total)));
}
