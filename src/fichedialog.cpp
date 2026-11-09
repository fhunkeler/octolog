#include "fichedialog.h"
#include "admin.h"

#include "charts.h"

#include "depositdialogs.h"
#include "loandialogs.h"
#include "maintenancedialogs.h"
#include "recorddialog.h"
#include "tablefill.h"
#include "ui.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
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

namespace Fiche {

void depositAction(QWidget *parent, int memberId)
{
    if (!Admin::require(parent))
        return;
    // Caution detenue : proposer sa cloture ou sa modification ; sinon l'enregistrer.
    const QList<Deposit> held = Database::instance().deposits(memberId, true);
    if (held.isEmpty()) {
        DepositUi::record(parent, memberId);
        return;
    }
    QMessageBox box(QMessageBox::Question, QObject::tr("Caution"),
                    QObject::tr("%1 a déjà une caution de %2 € (saison %3).")
                        .arg(held.first().memberLabel)
                        .arg(held.first().amount)
                        .arg(held.first().season),
                    QMessageBox::Cancel, parent);
    QPushButton *close = box.addButton(QObject::tr("Clôturer (restituée, détruite…)"), QMessageBox::AcceptRole);
    QPushButton *edit = box.addButton(QObject::tr("Modifier"), QMessageBox::ActionRole);
    QPushButton *add = box.addButton(QObject::tr("Nouvelle caution"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == close)
        DepositUi::close(parent, {held.first().id});
    else if (box.clickedButton() == edit)
        DepositUi::record(parent, memberId, &held.first());
    else if (box.clickedButton() == add)
        DepositUi::record(parent, memberId);
}

QString statusHtml(const EntityDef &def, const QVariantMap &rec, const QList<Loan> &activeLoans)
{
    if (def.isEquipment) {
        Database &db = Database::instance();
        const int id = rec.value(QStringLiteral("id")).toInt();
        QString first;
        const QList<MaintenanceRecord> all = db.maintenances(def.kind, id);
        QList<MaintenanceRecord> done;
        const MaintenanceRecord *open = nullptr;
        for (const MaintenanceRecord &m : all) {
            if (m.open())
                open = &m;
            else
                done << m;
        }
        if (!activeLoans.isEmpty()) {
            const Loan &l = activeLoans.first();
            first = QObject::tr("Emprunté par <a href=\"membres:%1\"><b>%2</b></a> depuis le %3 (%4)")
                        .arg(l.memberId)
                        .arg(l.memberLabel.toHtmlEscaped(), l.out.toString(QStringLiteral("dd/MM/yyyy")),
                             TableFill::durationText(l.durationDays()));
        } else if (open && !Admin::instance().isAdmin()) {
            first = QObject::tr("<b>En entretien</b> : ne peut pas être prêté pour l'instant");
        } else if (open) {
            first = QObject::tr("<b>En entretien</b> depuis le %1 : %2, responsable %3")
                        .arg(open->start.toString(QStringLiteral("dd/MM/yyyy")),
                             Maintenance::operationLabel(def.kind, open->operation).toHtmlEscaped(),
                             open->responsible.toHtmlEscaped());
        } else if (rec.value(QStringLiteral("hors_service")).toInt()) {
            first = QObject::tr("Hors service : ne peut pas être prêté");
        } else {
            first = QObject::tr("Disponible au local");
        }
        QStringList lines{first};
        if (!Admin::instance().isAdmin())
            return first; // echeances d'entretien : mode admin
        for (const Maintenance::Due &d : Maintenance::dues(def, rec, done))
            lines << QStringLiteral("<span style=\"color:%1\">●</span> %2 : %3")
                         .arg(Maintenance::stateColor(d.state).name(), d.op.label.toHtmlEscaped(),
                              Maintenance::dueText(d).toHtmlEscaped());
        if (def.kind == QLatin1String("blocs"))
            lines.last() += QObject::tr(" <small>(régime %1)</small>").arg(Maintenance::blocRegime(rec) == Maintenance::Standard ? QObject::tr("standard") : QObject::tr("dérogatoire TIV"));
        return lines.join(QStringLiteral("<br>"));
    }
    QStringList rights;
    for (const EntityDef *e : Schema::equipment())
        rights << (Schema::isYes(rec.value(e->rightField))
                       ? QStringLiteral("<span style=\"color:%1\">✔</span> %2").arg(Ui::success().name(), e->singular)
                       : QStringLiteral("<span style=\"color:%1\">✘</span> %2").arg(Ui::danger().name(), e->singular));
    QString s = QObject::tr("Droits de prêt : %1").arg(rights.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    // Caution (prets payants) : mode admin
    if (Admin::instance().isAdmin()) {
        const QList<Deposit> held = Database::instance().deposits(rec.value(QStringLiteral("id")).toInt(), true);
        if (!held.isEmpty()) {
            const Deposit &d = held.first();
            s += QObject::tr("<br><span style=\"color:%1\">●</span> Caution reçue : %2 € (%3%4) le %5, saison %6")
                     .arg(Charts::statusGood().name())
                     .arg(d.amount)
                     .arg(d.method.toHtmlEscaped(), d.number.isEmpty() ? QString() : QStringLiteral(" n° ") + d.number.toHtmlEscaped())
                     .arg(d.received.toString(QStringLiteral("dd/MM/yyyy")), d.season);
        } else if (Deposits::needsDeposit(rec)) {
            QStringList types;
            for (const EntityDef *e : Deposits::paidTypes(rec))
                types << e->singular;
            s += QObject::tr("<br><span style=\"color:%1\">●</span> <b>Caution manquante</b> (prêt payant : %2)")
                     .arg(Charts::statusCritical().name(), types.join(QStringLiteral(", ")));
        }
    }
    if (Schema::isInactive(rec))
        s += QObject::tr("<br>Absent du dernier import des adhérents");
    const QDate end = QDate::fromString(rec.value(QStringLiteral("adhesion_fin")).toString(), Qt::ISODate);
    if (end.isValid() && end < QDate::currentDate())
        s += QObject::tr("<br>Adhésion expirée le %1").arg(end.toString(QStringLiteral("dd/MM/yyyy")));
    return s;
}

void open(const QString &kind, int id, QWidget *parent)
{
    const EntityDef *def = Schema::byKind(kind);
    if (!def || id < 0)
        return;
    // Une seule fenetre par fiche : on remet au premier plan celle deja ouverte.
    QWidget *top = parent ? parent->window() : nullptr;
    const auto existing = top ? top->findChildren<FicheDialog *>() : QList<FicheDialog *>();
    for (FicheDialog *d : existing)
        if (&d->def() == def && d->recordId() == id) {
            d->raise();
            d->activateWindow();
            return;
        }
    auto *dlg = new FicheDialog(*def, id, top ? top : parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

} // namespace Fiche

FicheDialog::FicheDialog(const EntityDef &def, int id, QWidget *parent)
    : QDialog(parent)
    , m_def(def)
    , m_id(id)
{
        m_header = new Ui::Header(this);
    connect(m_header->details(), &QLabel::linkActivated, this, [this](const QString &link) {
        const QStringList parts = link.split(QLatin1Char(':'));
        if (parts.size() == 2)
            Fiche::open(parts[0], parts[1].toInt(), this);
    });

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto *ficheBox = new QGroupBox(tr("Caractéristiques"), this);
    auto *ficheLayout = new QVBoxLayout(ficheBox);
    ficheLayout->addWidget(m_scroll);

    const QString other = def.isEquipment ? tr("Adhérent") : tr("Matériel");
    m_activeBox = new QGroupBox(this);
    m_active = makeLoanTree({other, tr("Depuis le"), tr("Durée"), tr("Remarque")}, this);
    m_returnLoans = new QPushButton(Ui::icon(QStringLiteral("return")), tr("Rendre la sélection"), this);
    auto *activeLayout = new QVBoxLayout(m_activeBox);
    activeLayout->addWidget(m_active);
    auto *activeButtons = new QHBoxLayout;
    activeButtons->addWidget(new QLabel(tr("<small>Double-clic : ouvrir la fiche</small>"), this));
    activeButtons->addStretch();
    activeButtons->addWidget(m_returnLoans);
    activeLayout->addLayout(activeButtons);

    m_historyBox = new QGroupBox(this);
    m_history = makeLoanTree({other, tr("Emprunté le"), tr("Rendu le"), tr("Durée"), tr("Remarques")}, this);
    m_history->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *historyLayout = new QVBoxLayout(m_historyBox);
    historyLayout->addWidget(m_history);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(ficheBox);
    split->addWidget(m_activeBox);
    split->addWidget(m_historyBox);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 3);
    if (def.isEquipment) {
        // Entretiens : en cours et termines (responsable, actions, resultat, cout)
        m_maintBox = new QGroupBox(this);
        m_maint = makeLoanTree({tr("Opération"), tr("Responsable"), tr("Début"), tr("Fin"), tr("Résultat"), tr("Coût"),
                                tr("Actions effectuées")}, this);
        m_maint->setToolTip(tr("Double-clic sur un entretien en cours : le terminer"));
        auto *ml = new QVBoxLayout(m_maintBox);
        ml->addWidget(m_maint);
        split->addWidget(m_maintBox);
        split->setStretchFactor(3, 3);
        connect(m_maint, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
            if (it->data(0, Qt::UserRole + 10).toBool())
                MaintenanceUi::finish(this, {it->data(0, Qt::UserRole + 11).toInt()});
        });
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *edit = buttons->addButton(tr("&Modifier…"), QDialogButtonBox::ActionRole);
    edit->setIcon(Ui::icon(QStringLiteral("edit")));
    Admin::adminOnly(edit);
    m_lend = buttons->addButton(def.isEquipment ? tr("&Prêter à un adhérent…") : tr("&Emprunter du matériel…"),
                                QDialogButtonBox::ActionRole);
    m_lend->setIcon(Ui::icon(QStringLiteral("lend")));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(edit, &QPushButton::clicked, this, [this] {
        if (!Admin::require(this))
            return;
        RecordDialog dlg(m_def, Database::instance().record(m_def, m_id), this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        QString err;
        if (!Database::instance().update(m_def, m_id, dlg.values(), &err))
            QMessageBox::warning(this, tr("Modification"), err);
    });
    if (!def.isEquipment) {
        auto *depositButton = buttons->addButton(tr("&Caution…"), QDialogButtonBox::ActionRole);
        depositButton->setIcon(Ui::icon(QStringLiteral("deposit")));
        Admin::adminOnly(depositButton);
        connect(depositButton, &QPushButton::clicked, this, [this] { Fiche::depositAction(this, m_id); });
    }
    if (def.isEquipment) {
        auto *maintButton = buttons->addButton(tr("En&tretien"), QDialogButtonBox::ActionRole);
        maintButton->setIcon(Ui::icon(QStringLiteral("maintenance")));
        Admin::adminOnly(maintButton);
        auto *menu = new QMenu(maintButton);
        menu->addAction(Ui::icon(QStringLiteral("maintenance")), tr("Démarrer un entretien…"), this,
                        [this] { MaintenanceUi::start(this, m_def, {m_id}); });
        menu->addAction(Ui::icon(QStringLiteral("return")), tr("Terminer l'entretien…"), this, [this] {
            QList<int> ids;
            for (const MaintenanceRecord &m : Database::instance().maintenances(m_def.kind, m_id, true))
                ids << m.id;
            MaintenanceUi::finish(this, ids);
        });
        menu->addSeparator();
        menu->addAction(tr("Enregistrer un entretien effectué…"), this,
                        [this] { MaintenanceUi::start(this, m_def, {m_id}, true); });
        maintButton->setMenu(menu);
    }
    connect(m_lend, &QPushButton::clicked, this, [this] {
        if (m_def.isEquipment)
            LoanUi::lendEquipment(this, m_def, {m_id});
        else
            LoanUi::borrowForMember(this, m_id);
    });
    connect(m_returnLoans, &QPushButton::clicked, this, &FicheDialog::returnSelected);
    connect(m_active, &QTreeWidget::itemSelectionChanged, this, [this] {
        m_returnLoans->setEnabled(!m_active->selectedItems().isEmpty());
    });
    connect(m_active, &QTreeWidget::itemDoubleClicked, this, &FicheDialog::openOther);
    connect(m_history, &QTreeWidget::itemDoubleClicked, this, &FicheDialog::openOther);
    m_active->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_active, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_active->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        QAction *ret = menu.addAction(Ui::icon(QStringLiteral("return")),
                                      m_active->selectedItems().size() > 1 ? tr("Rendre la sélection")
                                                                           : tr("Rendre « %1 »").arg(item->text(0)),
                                      this, &FicheDialog::returnSelected);
        menu.setDefaultAction(ret);
        menu.addAction(Ui::icon(QStringLiteral("fiche")), tr("Ouvrir la fiche"), this,
                       [this, item] { openOther(item); });
        menu.exec(m_active->viewport()->mapToGlobal(pos));
    });
    connect(m_history, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (m_history->selectedItems().isEmpty())
            return;
        QMenu menu(this);
        menu.addAction(tr("Ouvrir la fiche"), this, [this] { openOther(m_history->currentItem()); });
        if (Admin::instance().isAdmin())
            menu.addAction(Ui::icon(QStringLiteral("delete")), tr("Supprimer l'entrée de l'historique…"),
                           this, &FicheDialog::deleteHistoryEntries);
        menu.exec(m_history->viewport()->mapToGlobal(pos));
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_header);
    layout->addWidget(split, 1);
    layout->addWidget(buttons);

    connect(&Database::instance(), &Database::changed, this, &FicheDialog::refresh);
    connect(&Admin::instance(), &Admin::changed, this, &FicheDialog::refresh);
    refresh();
    resize(780, 880);
}

void FicheDialog::refresh()
{
    Database &db = Database::instance();
    const QVariantMap rec = db.record(m_def, m_id);
    if (rec.isEmpty()) {
        close(); // fiche supprimee
        return;
    }
    const QString label = m_def.displayLabel(rec);
    setWindowTitle(tr("Fiche %1 — %2").arg(m_def.singular, label));
    setWindowIcon(Ui::kindIcon(m_def.kind));

    // Caracteristiques sur deux colonnes
    auto *host = new QWidget;
    auto *cols = new QHBoxLayout(host);
    QList<QPair<QString, QString>> rows;
    for (const Field &f : m_def.fields) {
        const QString text = Schema::formatValue(f, rec.value(f.key));
        if (!text.isEmpty())
            rows.append({f.label, text});
    }
    const int half = (rows.size() + 1) / 2;
    for (int part = 0; part < 2; ++part) {
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setLabelAlignment(Qt::AlignRight);
        for (int i = part * half; i < qMin(rows.size(), (part + 1) * half); ++i) {
            auto *value = new QLabel(rows[i].second);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setWordWrap(true);
            form->addRow(rows[i].first + QStringLiteral(" :"), value);
        }
        cols->addLayout(form, 1);
    }
    cols->setAlignment(Qt::AlignTop);
    m_scroll->setWidget(host);

    // Emprunts
    m_active->clear();
    m_history->clear();
    const QList<Loan> all = m_def.isEquipment ? db.loans(false, -1, m_def.kind, m_id) : db.loans(false, m_id);
    QList<Loan> active;
    for (const Loan &l : all) {
        auto *item = new QTreeWidgetItem;
        item->setData(0, LoanIdRole, l.id);
        item->setData(0, MemberRole, l.memberId);
        item->setData(0, KindRole, l.kind);
        item->setData(0, EquipRole, l.equipId);
        item->setText(0, m_def.isEquipment ? l.memberLabel : l.equipLabel);
        if (l.active()) {
            active << l;
            item->setText(1, l.out.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
            item->setText(2, TableFill::durationText(l.durationDays()));
            item->setText(3, l.notesOut);
            m_active->addTopLevelItem(item);
        } else {
            item->setText(1, l.out.toString(QStringLiteral("dd/MM/yyyy")));
            item->setText(2, l.back.toString(QStringLiteral("dd/MM/yyyy")));
            item->setText(3, TableFill::durationText(l.durationDays()));
            QStringList notes;
            if (!l.notesOut.isEmpty())
                notes << l.notesOut;
            if (!l.notesBack.isEmpty())
                notes << tr("retour : %1").arg(l.notesBack);
            item->setText(4, notes.join(QStringLiteral(" · ")));
            m_history->addTopLevelItem(item);
        }
    }
    for (QTreeWidget *t : {m_active, m_history})
        for (int c = 0; c < t->columnCount() - 1; ++c)
            t->resizeColumnToContents(c);
    m_header->setRecord(m_def, rec, active.size(),
                        m_def.isEquipment && !db.maintenances(m_def.kind, m_id, true).isEmpty());
    m_header->setDetails(Fiche::statusHtml(m_def, rec, active));
    m_activeBox->setTitle(m_def.isEquipment ? tr("Emprunt en cours")
                                            : tr("Matériel emprunté (%1)").arg(active.size()));
    m_historyBox->setTitle(tr("Historique des emprunts (%1)").arg(m_history->topLevelItemCount()));
    m_returnLoans->setEnabled(false);

    if (m_maintBox)
        m_maintBox->setVisible(Admin::instance().isAdmin());
    if (m_maint && Admin::instance().isAdmin()) {
        m_maint->clear();
        double total = 0;
        const QList<MaintenanceRecord> list = db.maintenances(m_def.kind, m_id);
        for (const MaintenanceRecord &m : list) {
            auto *it = new QTreeWidgetItem({Maintenance::operationLabel(m.kind, m.operation), m.responsible,
                                            m.start.toString(QStringLiteral("dd/MM/yyyy")),
                                            m.open() ? tr("en cours") : m.end.toString(QStringLiteral("dd/MM/yyyy")),
                                            m.open() ? QString() : m.result,
                                            m.cost.isValid() && !m.cost.toString().isEmpty()
                                                ? QLocale().toCurrencyString(m.cost.toDouble(), QStringLiteral("€"))
                                                : QString(),
                                            QString(m.actions).replace(QLatin1Char('\n'), QStringLiteral(" · "))});
            it->setToolTip(6, m.actions);
            it->setData(0, Qt::UserRole + 10, m.open());
            it->setData(0, Qt::UserRole + 11, m.id);
            it->setIcon(0, Ui::dot(m.open() ? Charts::inMaintenance() : m.conform() ? Charts::statusGood() : Charts::statusCritical()));
            if (m.open()) {
                QFont f = it->font(0);
                f.setBold(true);
                for (int c = 0; c < 7; ++c)
                    it->setFont(c, f);
            }
            total += m.cost.toDouble();
            m_maint->addTopLevelItem(it);
        }
        for (int c = 0; c < m_maint->columnCount() - 1; ++c)
            m_maint->resizeColumnToContents(c);
        m_maintBox->setTitle(total > 0 ? tr("Entretiens (%1) — coût total %2")
                                             .arg(list.size())
                                             .arg(QLocale().toCurrencyString(total, QStringLiteral("€")))
                                       : tr("Entretiens (%1)").arg(list.size()));
    }
}

void FicheDialog::openOther(QTreeWidgetItem *item)
{
    if (!item)
        return;
    if (m_def.isEquipment)
        Fiche::open(Schema::members().kind, item->data(0, MemberRole).toInt(), this);
    else
        Fiche::open(item->data(0, KindRole).toString(), item->data(0, EquipRole).toInt(), this);
}

void FicheDialog::returnSelected()
{
    QList<int> ids;
    for (QTreeWidgetItem *it : m_active->selectedItems())
        ids << it->data(0, LoanIdRole).toInt();
    LoanUi::returnLoans(this, ids);
}

void FicheDialog::deleteHistoryEntries()
{
    if (!Admin::require(this))
        return;
    const auto items = m_history->selectedItems();
    if (items.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Supprimer de l'historique"),
                              tr("Supprimer définitivement %n entrée(s) de l'historique ?\n"
                                 "À utiliser uniquement pour corriger une erreur de saisie.", nullptr, items.size()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    QList<int> ids;
    for (QTreeWidgetItem *it : items)
        ids << it->data(0, LoanIdRole).toInt();
    Database &db = Database::instance();
    db.beginBatch();
    for (int id : ids) {
        QString err;
        db.removeLoan(id, &err);
    }
    db.endBatch();
}
