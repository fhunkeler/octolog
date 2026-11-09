#include "maintenancetab.h"
#include "admin.h"

#include "charts.h"

#include "database.h"
#include "fichedialog.h"
#include "maintenancedialogs.h"
#include "searchabletable.h"
#include "searchproxy.h"
#include "tablefill.h"
#include "ui.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace {
QString dateText(const QDate &d)
{
    return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QString();
}

QString moneyText(const QVariant &v)
{
    return v.isValid() && !v.toString().isEmpty() ? QLocale().toCurrencyString(v.toDouble(), QStringLiteral("€")) : QString();
}

QVariantMap meta(SearchableTable *t, int id)
{
    for (int r = 0; r < t->model()->rowCount(); ++r) {
        const QStandardItem *it = t->model()->item(r, 0);
        if (it->data(Roles::Id).toInt() == id)
            return it->data(Roles::Record).toMap();
    }
    return {};
}
} // namespace

MaintenanceTab::MaintenanceTab(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabWidget(this))
    , m_dues(new SearchableTable(QStringLiteral("entretien_echeances"), this))
    , m_open(new SearchableTable(QStringLiteral("entretien_en_cours"), this))
    , m_journal(new SearchableTable(QStringLiteral("entretien_journal"), this))
    , m_total(new QLabel(this))
{
    m_start = new QPushButton(Ui::icon(QStringLiteral("maintenance")), tr("&Démarrer un entretien…"), this);
    m_record = new QPushButton(tr("Enregistrer un entretien &effectué…"), this);
    m_finish = new QPushButton(Ui::icon(QStringLiteral("return")), tr("&Terminer l'entretien…"), this);
    m_cancel = new QPushButton(Ui::icon(QStringLiteral("delete")), tr("Annuler l'entretien"), this);
    m_delete = new QPushButton(Ui::icon(QStringLiteral("delete")), tr("Supprimer l'entrée"), this);
    m_fiche = new QPushButton(Ui::icon(QStringLiteral("fiche")), tr("&Fiche du matériel…"), this);
    QFont bold = m_start->font();
    bold.setBold(true);
    m_start->setFont(bold);
    m_finish->setFont(bold);
    m_cancel->setToolTip(tr("Supprime un entretien démarré par erreur : le matériel redevient disponible"));
    m_delete->setToolTip(tr("Supprime une entrée erronée du journal"));

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_start);
    actions->addWidget(m_record);
    actions->addWidget(m_finish);
    actions->addWidget(m_cancel);
    actions->addWidget(m_delete);
    actions->addStretch();
    actions->addWidget(m_fiche);

    // Echeances
    m_dues->setDefaultSort(QStringLiteral("Échéance"), Qt::AscendingOrder);
    auto stateIs = [](std::initializer_list<Maintenance::State> states) {
        QList<int> wanted;
        for (Maintenance::State s : states)
            wanted << int(s);
        return [wanted](const QStandardItem *it) {
            return wanted.contains(it->data(Roles::Maintenance).toInt()) && it->data(Roles::Status).toString() != TableFill::OutOfService;
        };
    };
    m_dues->addStatusFilter(tr("Tous"), {});
    m_dues->addStatusFilter(tr("À faire (en retard ou sous 30 j)"), stateIs({Maintenance::State::Overdue, Maintenance::State::Soon}));
    m_dues->addStatusFilter(tr("En retard"), stateIs({Maintenance::State::Overdue}));
    m_dues->addStatusFilter(tr("À prévoir (30 j)"), stateIs({Maintenance::State::Soon}));
    m_dues->addStatusFilter(tr("À jour"), stateIs({Maintenance::State::Ok}));
    m_dues->addStatusFilter(tr("Sans obligation (gilets…)"), stateIs({Maintenance::State::None}));
    for (const EntityDef *def : Schema::equipment()) {
        const QString kind = def->kind;
        m_dues->addStatusFilter(def->label, [kind](const QStandardItem *it) {
            return it->data(Roles::Record).toMap().value(QStringLiteral("kind")).toString() == kind;
        });
    }
    m_dues->setStatusFilter(1);

    m_open->setDefaultSort(QStringLiteral("Début"), Qt::AscendingOrder);
    m_journal->setDefaultSort(QStringLiteral("Fin"), Qt::DescendingOrder);
    m_journal->addStatusFilter(tr("Tous les résultats"), {});
    m_journal->addStatusFilter(Maintenance::Conform, [](const QStandardItem *it) {
        return it->data(Roles::Status).toString() == Maintenance::Conform;
    });
    m_journal->addStatusFilter(Maintenance::NonConform, [](const QStandardItem *it) {
        return it->data(Roles::Status).toString() == Maintenance::NonConform;
    });

    auto *journalPage = new QWidget(this);
    auto *jl = new QVBoxLayout(journalPage);
    jl->setContentsMargins(0, 0, 0, 0);
    jl->addWidget(m_journal, 1);
    jl->addWidget(m_total, 0, Qt::AlignRight);

    m_tabs->setIconSize(QSize(18, 18));
    m_tabs->addTab(m_dues, Ui::icon(QStringLiteral("clock")), tr("Matériel et échéances"));
    m_tabs->addTab(m_open, Ui::icon(QStringLiteral("maintenance")), tr("En cours"));
    m_tabs->addTab(journalPage, Ui::icon(QStringLiteral("fiche")), tr("Journal"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(actions);
    layout->addWidget(Ui::separator(this));
    layout->addWidget(m_tabs, 1);

    connect(m_tabs, &QTabWidget::currentChanged, this, &MaintenanceTab::updateButtons);
    for (SearchableTable *t : {m_dues, m_open, m_journal})
        connect(t, &SearchableTable::selectionChanged, this, &MaintenanceTab::updateButtons);
    connect(m_journal->proxy(), &QAbstractItemModel::rowsInserted, this, &MaintenanceTab::updateButtons);
    connect(m_journal->proxy(), &QAbstractItemModel::rowsRemoved, this, &MaintenanceTab::updateButtons);
    connect(m_journal->proxy(), &QAbstractItemModel::modelReset, this, &MaintenanceTab::updateButtons);
    connect(m_journal->proxy(), &QAbstractItemModel::layoutChanged, this, &MaintenanceTab::updateButtons);

    connect(m_start, &QPushButton::clicked, this, [this] {
        QString kind;
        QList<int> ids;
        if (selectedEquipment(&kind, &ids))
            MaintenanceUi::start(this, *Schema::byKind(kind), ids);
    });
    connect(m_record, &QPushButton::clicked, this, [this] {
        QString kind;
        QList<int> ids;
        if (selectedEquipment(&kind, &ids))
            MaintenanceUi::start(this, *Schema::byKind(kind), ids, true);
    });
    connect(m_finish, &QPushButton::clicked, this, [this] { MaintenanceUi::finish(this, m_open->selectedIds()); });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        const QList<int> ids = m_open->selectedIds();
        if (!Admin::require(this))
            return;
        if (ids.isEmpty()
            || QMessageBox::question(this, tr("Annuler l'entretien"),
                                     tr("Annuler %n entretien(s) en cours ? Le matériel redevient disponible.", nullptr, ids.size()))
                   != QMessageBox::Yes)
            return;
        Database::instance().beginBatch();
        for (int id : ids)
            Database::instance().removeMaintenance(id, nullptr);
        Database::instance().endBatch();
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const QList<int> ids = m_journal->selectedIds();
        if (!Admin::require(this))
            return;
        if (ids.isEmpty()
            || QMessageBox::question(this, tr("Supprimer du journal"),
                                     tr("Supprimer définitivement %n entrée(s) du journal des entretiens ?", nullptr, ids.size()),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                   != QMessageBox::Yes)
            return;
        Database::instance().beginBatch();
        for (int id : ids)
            Database::instance().removeMaintenance(id, nullptr);
        Database::instance().endBatch();
    });
    connect(m_fiche, &QPushButton::clicked, this, [this] {
        if (SearchableTable *t = currentTable())
            openFiche(t);
    });
    // Double-clic : action principale de chaque liste
    connect(m_dues, &SearchableTable::activated, this, [this] {
        (Admin::instance().isAdmin() ? m_start : m_fiche)->click();
    });
    connect(m_open, &SearchableTable::activated, this, [this] {
        (Admin::instance().isAdmin() ? m_finish : m_fiche)->click();
    });
    connect(m_journal, &SearchableTable::activated, m_fiche, &QPushButton::click);

    for (SearchableTable *t : {m_dues, m_open, m_journal}) {
        t->view()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(t->view(), &QWidget::customContextMenuRequested, this, [this, t](const QPoint &pos) {
            QMenu menu(this);
            for (QPushButton *b : {m_start, m_record, m_finish, m_cancel, m_delete, m_fiche})
                if (b->isVisible())
                    menu.addAction(b->icon(), b->text(), b, &QPushButton::click)->setEnabled(b->isEnabled());
            if (!menu.actions().isEmpty())
                menu.setDefaultAction(menu.actions().first());
            menu.exec(t->view()->viewport()->mapToGlobal(pos));
        });
    }

    connect(&Database::instance(), &Database::changed, this, &MaintenanceTab::reload);
    connect(&Admin::instance(), &Admin::changed, this, &MaintenanceTab::updateButtons);
    reload();
}

SearchableTable *MaintenanceTab::currentTable() const
{
    switch (m_tabs->currentIndex()) {
    case 0:
        return m_dues;
    case 1:
        return m_open;
    default:
        return m_journal;
    }
}

void MaintenanceTab::reload()
{
    fillDues();
    fillOpen();
    fillJournal();
    updateButtons();
}

void MaintenanceTab::fillDues()
{
    Database &db = Database::instance();
    const LoanIndex idx = db.activeLoanIndex();
    m_dues->beginReset();
    m_dues->setHeaders({tr("Matériel"), tr("Type"), tr("Opération"), tr("Dernière"), tr("Échéance"), tr("État"),
                        tr("Jours restants"), tr("Régime"), tr("Disponibilité")});
    int row = 0, todo = 0;
    for (const EntityDef *def : Schema::equipment()) {
        for (const QVariantMap &rec : db.records(*def)) {
            const int id = rec.value(QStringLiteral("id")).toInt();
            const bool hs = rec.value(QStringLiteral("hors_service")).toInt() != 0;
            const QString availability = idx.byEquipment.value(def->kind).contains(id)     ? TableFill::Borrowed
                                         : idx.inMaintenance.value(def->kind).contains(id) ? TableFill::InMaintenance
                                         : hs                                               ? TableFill::OutOfService
                                                                                            : TableFill::Available;
            for (const Maintenance::Due &d : Maintenance::dues(*def, rec, idx.journal.value(def->kind).value(id))) {
                // Gilets (stabs) : pas d'echeance legale, mais entretien possible depuis cette liste.
                const bool scheduled = d.months > 0;
                QList<QStandardItem *> items;
                items << makeItem(def->displayLabel(rec));
                items.first()->setIcon(Ui::kindIcon(def->kind));
                items << makeItem(def->label) << makeItem(d.op.label) << makeItem(dateText(d.last), d.last);
                if (!scheduled)
                    items << makeItem(QStringLiteral("—"), QDate(9999, 12, 31));
                else
                    items << makeItem(d.next.isValid() ? dateText(d.next) : tr("à faire"),
                                      d.next.isValid() ? QVariant(d.next) : QVariant(QDate(1900, 1, 1)));
                QStandardItem *state = makeItem(Maintenance::stateText(d.state), int(d.state));
                state->setIcon(Ui::dot(Maintenance::stateColor(d.state)));
                items << state;
                items << (d.next.isValid() ? makeItem(QString::number(d.daysLeft), d.daysLeft) : makeItem({}));
                if (scheduled && (d.daysLeft < 0 || !d.next.isValid()))
                    items.last()->setForeground(Ui::danger());
                items << makeItem(def->kind == QLatin1String("blocs") ? Maintenance::blocRegime(rec) : QString())
                      << makeItem(availability);
                if (hs)
                    for (QStandardItem *it : items)
                        it->setForeground(QPalette().color(QPalette::Disabled, QPalette::Text));
                items.first()->setData(QVariantMap{{"kind", def->kind}, {"equipId", id}}, Roles::Record);
                items.first()->setData(int(d.state), Roles::Maintenance);
                items.first()->setData(hs ? TableFill::OutOfService : availability, Roles::Status);
                m_dues->appendRow(++row, items);
                if (!hs && (d.state == Maintenance::State::Overdue || d.state == Maintenance::State::Soon))
                    ++todo;
            }
        }
    }
    m_dues->endReset();
    m_tabs->setTabText(0, todo ? tr("Matériel et échéances (%1 à faire)").arg(todo) : tr("Matériel et échéances"));
}

void MaintenanceTab::fillOpen()
{
    m_open->beginReset();
    m_open->setHeaders({tr("Matériel"), tr("Type"), tr("Opération"), tr("Responsable"), tr("Début"), tr("Durée (jours)")});
    const QList<MaintenanceRecord> open = Database::instance().maintenances({}, -1, true);
    for (const MaintenanceRecord &m : open) {
        const EntityDef *def = Schema::byKind(m.kind);
        const int days = int(m.start.daysTo(QDate::currentDate()));
        QList<QStandardItem *> items;
        items << makeItem(m.equipLabel);
        items.first()->setIcon(Ui::kindIcon(m.kind));
        items << makeItem(def ? def->label : m.kind) << makeItem(Maintenance::operationLabel(m.kind, m.operation))
              << makeItem(m.responsible) << makeItem(dateText(m.start), m.start) << makeItem(QString::number(days), days);
        items.first()->setData(QVariantMap{{"kind", m.kind}, {"equipId", m.equipId}}, Roles::Record);
        m_open->appendRow(m.id, items);
    }
    m_open->endReset();
    m_tabs->setTabText(1, open.isEmpty() ? tr("En cours") : tr("En cours (%1)").arg(open.size()));
}

void MaintenanceTab::fillJournal()
{
    m_journal->beginReset();
    m_journal->setHeaders({tr("Fin"), tr("Matériel"), tr("Type"), tr("Opération"), tr("Responsable"), tr("Début"),
                           tr("Résultat"), tr("Coût"), tr("Actions effectuées")});
    for (const MaintenanceRecord &m : Database::instance().maintenances()) {
        if (m.open())
            continue;
        const EntityDef *def = Schema::byKind(m.kind);
        QList<QStandardItem *> items;
        items << makeItem(dateText(m.end), m.end) << makeItem(m.equipLabel);
        items.at(1)->setIcon(Ui::kindIcon(m.kind));
        items << makeItem(def ? def->label : m.kind) << makeItem(Maintenance::operationLabel(m.kind, m.operation))
              << makeItem(m.responsible) << makeItem(dateText(m.start), m.start);
        QStandardItem *result = makeItem(m.result);
        result->setIcon(Ui::dot(m.conform() ? Charts::statusGood() : Charts::statusCritical()));
        items << result;
        items << makeItem(moneyText(m.cost), m.cost.isValid() && !m.cost.toString().isEmpty() ? QVariant(m.cost.toDouble()) : QVariant());
        items << makeItem(QString(m.actions).replace(QLatin1Char('\n'), QStringLiteral(" · ")));
        items.last()->setToolTip(m.actions);
        items.first()->setData(QVariantMap{{"kind", m.kind}, {"equipId", m.equipId}}, Roles::Record);
        items.first()->setData(m.conform() ? Maintenance::Conform : Maintenance::NonConform, Roles::Status);
        m_journal->appendRow(m.id, items);
    }
    m_journal->endReset();
}

void MaintenanceTab::updateButtons()
{
    const int page = m_tabs->currentIndex();
    const bool admin = Admin::instance().isAdmin();
    m_start->setVisible(admin && page == 0);
    m_record->setVisible(admin && page == 0);
    m_finish->setVisible(admin && page == 1);
    m_cancel->setVisible(admin && page == 1);
    m_delete->setVisible(admin && page == 2);
    SearchableTable *t = currentTable();
    const int n = t->selectedIds().size();
    m_start->setEnabled(n > 0);
    m_record->setEnabled(n > 0);
    m_finish->setEnabled(n > 0);
    m_cancel->setEnabled(n > 0);
    m_delete->setEnabled(n > 0);
    m_fiche->setEnabled(n == 1);

    // Cout total des lignes affichees du journal
    double total = 0;
    int counted = 0;
    const int costCol = 7;
    for (int r = 0; r < m_journal->proxy()->rowCount(); ++r) {
        const QVariant v = m_journal->proxy()->index(r, costCol).data(Roles::Sort);
        if (v.isValid()) {
            total += v.toDouble();
            ++counted;
        }
    }
    m_total->setText(tr("Coût total des entretiens affichés : <b>%1</b> (%n entretien(s) chiffré(s))", nullptr, counted)
                         .arg(QLocale().toCurrencyString(total, QStringLiteral("€"))));
    m_total->setVisible(page == 2);
}

bool MaintenanceTab::selectedEquipment(QString *kind, QList<int> *ids)
{
    QSet<QString> kinds;
    QList<int> out;
    for (int rowId : m_dues->selectedIds()) {
        const QVariantMap m = meta(m_dues, rowId);
        kinds.insert(m.value(QStringLiteral("kind")).toString());
        const int id = m.value(QStringLiteral("equipId")).toInt();
        if (!out.contains(id))
            out << id;
    }
    if (kinds.size() != 1) {
        if (kinds.size() > 1)
            QMessageBox::information(this, tr("Entretien"),
                                     tr("Sélectionnez du matériel d'un seul type (blocs, détendeurs ou gilets) à la fois."));
        return false;
    }
    *kind = *kinds.begin();
    *ids = out;
    return true;
}

void MaintenanceTab::openFiche(SearchableTable *table)
{
    const QVariantMap m = meta(table, table->currentId());
    if (!m.isEmpty())
        Fiche::open(m.value(QStringLiteral("kind")).toString(), m.value(QStringLiteral("equipId")).toInt(), this);
}
