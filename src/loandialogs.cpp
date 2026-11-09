#include "loandialogs.h"

#include "admin.h"
#include "database.h"
#include "depositdialogs.h"
#include "fichedialog.h"
#include "searchabletable.h"
#include "searchproxy.h"
#include "tablefill.h"
#include "ui.h"

#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace {

struct Item {
    const EntityDef *def;
    int id;
};

// Date/heure + remarque, communs aux dialogues d'emprunt et de retour.
struct WhenAndNotes {
    QDateTimeEdit *when;
    QLineEdit *notes;

    void addTo(QFormLayout *form, QWidget *parent, const QString &dateLabel, const QString &notesLabel)
    {
        when = new QDateTimeEdit(QDateTime::currentDateTime(), parent);
        when->setCalendarPopup(true);
        when->setDisplayFormat(QStringLiteral("dd/MM/yyyy HH:mm"));
        notes = new QLineEdit(parent);
        notes->setPlaceholderText(QObject::tr("facultatif"));
        form->addRow(dateLabel, when);
        form->addRow(notesLabel, notes);
    }
};

// Verifie droits d'emprunt et validite de l'adhesion ; demande confirmation si besoin.
// Pret payant sans caution : proposer de l'enregistrer avant de preter.
bool confirmDeposit(QWidget *parent, const QVariantMap &member, const QList<Item> &items)
{
    const int memberId = member.value(QStringLiteral("id")).toInt();
    const QList<const EntityDef *> paid = Deposits::paidTypes(member);
    QStringList concerned;
    for (const Item &it : items)
        if (paid.contains(it.def) && !concerned.contains(it.def->singular))
            concerned << it.def->singular;
    if (!Admin::instance().isAdmin() || concerned.isEmpty() || !Database::instance().deposits(memberId, true).isEmpty())
        return true;
    QMessageBox box(QMessageBox::Warning, QObject::tr("Caution manquante"),
                    QObject::tr("%1 a un prêt payant (%2) mais aucune caution n'est enregistrée.")
                        .arg(Schema::members().displayLabel(member), concerned.join(QStringLiteral(", "))),
                    QMessageBox::Cancel, parent);
    QPushButton *record = box.addButton(QObject::tr("Enregistrer la caution…"), QMessageBox::AcceptRole);
    QPushButton *without = box.addButton(QObject::tr("Prêter sans caution"), QMessageBox::DestructiveRole);
    box.setDefaultButton(record);
    box.exec();
    if (box.clickedButton() == record)
        return DepositUi::record(parent, memberId);
    return box.clickedButton() == without;
}

bool confirmRights(QWidget *parent, const QVariantMap &member, const QList<Item> &items)
{
    const EntityDef &mdef = Schema::members();
    QStringList problems;
    QList<const EntityDef *> seen;
    for (const Item &it : items) {
        if (seen.contains(it.def))
            continue;
        seen << it.def;
        if (!Schema::isYes(member.value(it.def->rightField)))
            problems << QObject::tr("n'a pas le droit d'emprunter de %1").arg(it.def->singular);
    }
    // Entretien obligatoire depasse (bloc hors delai TIV / requalification, detendeur non revise)
    const LoanIndex idx = Database::instance().activeLoanIndex();
    for (const Item &it : items) {
        const QVariantMap rec = Database::instance().record(*it.def, it.id);
        for (const Maintenance::Due &d : Maintenance::dues(*it.def, rec, idx.journal.value(it.def->kind).value(it.id)))
            if (d.op.mandatory && d.state == Maintenance::State::Overdue)
                problems << QObject::tr("emprunterait « %1 » : %2 dépassée (%3)")
                                .arg(it.def->displayLabel(rec), d.op.label.toLower(), Maintenance::dueText(d).toLower());
    }
    if (Schema::isInactive(member))
        problems << QObject::tr("est désactivé (absent du dernier import des adhérents)");
    const QDate end = QDate::fromString(member.value(QStringLiteral("adhesion_fin")).toString(), Qt::ISODate);
    if (end.isValid() && end < QDate::currentDate())
        problems << QObject::tr("a une adhésion expirée depuis le %1").arg(end.toString(QStringLiteral("dd/MM/yyyy")));
    if (problems.isEmpty())
        return true;
    QMessageBox box(QMessageBox::Warning, QObject::tr("Vérification de l'emprunt"),
                    QObject::tr("%1 :\n• %2\n\nEnregistrer l'emprunt malgré tout ?")
                        .arg(mdef.displayLabel(member), problems.join(QStringLiteral("\n• "))),
                    QMessageBox::Yes | QMessageBox::No, parent);
    box.setDefaultButton(QMessageBox::No);
    box.button(QMessageBox::Yes)->setText(QObject::tr("Prêter quand même"));
    box.button(QMessageBox::No)->setText(QObject::tr("Annuler"));
    return box.exec() == QMessageBox::Yes;
}

// Enregistre les emprunts ; propose de transferer le materiel deja emprunte par un autre adherent.
bool doBorrow(QWidget *parent, int memberId, const QList<Item> &items, const QDateTime &when,
              const QString &notes)
{
    Database &db = Database::instance();
    const QVariantMap member = db.record(Schema::members(), memberId);
    if (!confirmRights(parent, member, items) || !confirmDeposit(parent, member, items))
        return false;

    QStringList errors;
    int done = 0;
    db.beginBatch();
    for (const Item &it : items) {
        const QList<Loan> current = db.loans(true, -1, it.def->kind, it.id);
        if (!current.isEmpty()) {
            const Loan &l = current.first();
            if (l.memberId == memberId)
                continue; // deja emprunte par cet adherent
            const auto answer = QMessageBox::question(
                parent, QObject::tr("Matériel déjà emprunté"),
                QObject::tr("« %1 » est actuellement emprunté par %2 depuis le %3.\n\n"
                            "Enregistrer son retour et le prêter à %4 ?")
                    .arg(l.equipLabel, l.memberLabel, l.out.toString(QStringLiteral("dd/MM/yyyy")),
                         Schema::members().displayLabel(member)));
            if (answer != QMessageBox::Yes)
                continue;
            QString err;
            if (!db.giveBack(l.id, when, QObject::tr("Transféré à %1").arg(Schema::members().displayLabel(member)), &err)) {
                errors << err;
                continue;
            }
        }
        QString err;
        if (db.borrow(memberId, it.def->kind, it.id, when, notes, &err))
            ++done;
        else
            errors << err;
    }
    db.endBatch();
    if (!errors.isEmpty())
        QMessageBox::warning(parent, QObject::tr("Emprunt"), errors.join('\n'));
    return done > 0;
}

// ---------------------------------------------------------------------------
// Choix du materiel pour un adherent

class EquipmentPicker : public QDialog
{
public:
    EquipmentPicker(const QVariantMap &member, QWidget *parent)
        : QDialog(parent)
    {
        const QString name = Schema::members().displayLabel(member);
        setWindowTitle(tr("Emprunt de matériel — %1").arg(name));

        auto *head = new Ui::Header(this);
        const int memberId = member.value(QStringLiteral("id")).toInt();
        const QList<Loan> current = Database::instance().loans(true, memberId);
        head->setRecord(Schema::members(), member, current.size());
        head->setDetails(Fiche::statusHtml(Schema::members(), member, current));
        m_tabs = new QTabWidget(this);
        m_tabs->setIconSize(QSize(20, 20));
        const LoanIndex index = Database::instance().activeLoanIndex();
        for (const EntityDef *def : Schema::equipment()) {
            auto *t = new SearchableTable(QStringLiteral("picker_") + def->kind, this);
            TableFill::addStandardFilters(t, *def);
            TableFill::entities(t, *def, index);
            t->setStatusFilter(1); // disponibles
            t->view()->clearSelection();
            m_tables << t;
            m_tabs->addTab(t, Ui::kindIcon(def->kind), def->label);
            if (!Schema::isYes(member.value(def->rightField)))
                m_tabs->setTabToolTip(m_tabs->count() - 1, tr("%1 n'a pas le droit de prêt de %2").arg(name, def->singular));
            connect(t, &SearchableTable::selectionChanged, this, [this] { updateSummary(); });
            // Un clic ajoute ou retire l'element : plusieurs equipements sans Ctrl.
            t->view()->setSelectionMode(QAbstractItemView::MultiSelection);
        }
        // Onglet par defaut : premier type autorise
        for (int i = 0; i < Schema::equipment().size(); ++i)
            if (Schema::isYes(member.value(Schema::equipment()[i]->rightField))) {
                m_tabs->setCurrentIndex(i);
                break;
            }

        m_summary = new QLabel(this);
        m_summary->setWordWrap(true);
        auto *clear = new QPushButton(tr("Vider la sélection"), this);
        connect(clear, &QPushButton::clicked, this, [this] {
            for (SearchableTable *t : m_tables)
                t->view()->clearSelection();
        });
        auto *selRow = new QHBoxLayout;
        selRow->addWidget(m_summary, 1);
        selRow->addWidget(clear);
        auto *form = new QFormLayout;
        form->addRow(tr("Sélection :"), selRow);
        m_wn.addTo(form, this, tr("Date d'emprunt :"), tr("Remarque :"));

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Enregistrer l'emprunt"));
        m_buttons->button(QDialogButtonBox::Ok)->setIcon(Ui::icon(QStringLiteral("lend")));
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *l = new QVBoxLayout(this);
        l->addWidget(head);
        l->addWidget(new QLabel(tr("Cliquez sur chaque équipement à emprunter (un second clic le retire). "
                                   "Vous pouvez en choisir plusieurs, dans plusieurs onglets."), this));
        l->addWidget(m_tabs, 1);
        l->addLayout(form);
        l->addWidget(m_buttons);
        resize(1000, 640);
        updateSummary();
        QMetaObject::invokeMethod(this, [this] {
            if (auto *t = qobject_cast<SearchableTable *>(m_tabs->currentWidget()))
                t->focusSearch();
        }, Qt::QueuedConnection);
    }

    QList<Item> selection() const
    {
        QList<Item> out;
        for (int i = 0; i < m_tables.size(); ++i)
            for (int id : m_tables[i]->selectedIds())
                out.append({Schema::equipment()[i], id});
        return out;
    }

    WhenAndNotes m_wn;

private:
    void updateSummary()
    {
        QStringList labels;
        for (int i = 0; i < m_tables.size(); ++i) {
            const int n = m_tables[i]->selectedIds().size();
            const EntityDef *def = Schema::equipment()[i];
            m_tabs->setTabText(i, n ? QStringLiteral("%1 (%2)").arg(def->label).arg(n) : def->label);
            for (int id : m_tables[i]->selectedIds())
                labels << Database::instance().label(def->kind, id);
        }
        m_summary->setText(labels.isEmpty() ? tr("aucun matériel sélectionné")
                                            : tr("<b>%n équipement(s)</b> : %1", nullptr, labels.size())
                                                  .arg(labels.join(QStringLiteral(", ")).toHtmlEscaped()));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!labels.isEmpty());
    }

    QTabWidget *m_tabs;
    QList<SearchableTable *> m_tables;
    QLabel *m_summary;
    QDialogButtonBox *m_buttons;
};

// ---------------------------------------------------------------------------
// Choix de l'adherent pour du materiel

class MemberPicker : public QDialog
{
public:
    MemberPicker(const EntityDef &def, const QStringList &labels, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr("Prêter %1").arg(labels.join(QStringLiteral(", "))));
        auto *head = new Ui::Header(this);
        head->setEmpty(def.kind, labels.join(QStringLiteral(", ")));
        head->setDetails(tr("%1 à prêter — choisissez l'adhérent emprunteur ci-dessous.")
                                     .arg(labels.size() > 1 ? def.label : def.singular.left(1).toUpper() + def.singular.mid(1)));
        m_table = new SearchableTable(QStringLiteral("picker_membres"), this);
        m_table->setMultiSelection(false);
        TableFill::addStandardFilters(m_table, Schema::members());
        TableFill::entities(m_table, Schema::members(), Database::instance().activeLoanIndex());
        m_table->setStatusFilter(TableFill::rightFilterLabel(def));
        m_table->view()->clearSelection();
        connect(m_table, &SearchableTable::activated, this, [this] { accept(); });

        auto *form = new QFormLayout;
        m_wn.addTo(form, this, tr("Date d'emprunt :"), tr("Remarque :"));
        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Prêter"));
        m_buttons->button(QDialogButtonBox::Ok)->setIcon(Ui::icon(QStringLiteral("lend")));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        connect(m_table, &SearchableTable::selectionChanged, this, [this] {
            m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_table->currentId() >= 0);
        });
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *l = new QVBoxLayout(this);
        l->addWidget(head);
        l->addWidget(new QLabel(tr("Choisissez l'adhérent emprunteur :"), this));
        l->addWidget(m_table, 1);
        l->addLayout(form);
        l->addWidget(m_buttons);
        resize(1000, 600);
        QMetaObject::invokeMethod(this, [this] { m_table->focusSearch(); }, Qt::QueuedConnection);
    }

    int memberId() const { return m_table->currentId(); }
    WhenAndNotes m_wn;

private:
    SearchableTable *m_table;
    QDialogButtonBox *m_buttons;
};

} // namespace

namespace LoanUi {

bool borrowForMember(QWidget *parent, int memberId)
{
    const QVariantMap member = Database::instance().record(Schema::members(), memberId);
    if (member.isEmpty())
        return false;
    EquipmentPicker dlg(member, parent);
    if (dlg.exec() != QDialog::Accepted || dlg.selection().isEmpty())
        return false;
    return doBorrow(parent, memberId, dlg.selection(), dlg.m_wn.when->dateTime(), dlg.m_wn.notes->text());
}

bool lendEquipment(QWidget *parent, const EntityDef &def, const QList<int> &equipIds)
{
    if (equipIds.isEmpty())
        return false;
    Database &db = Database::instance();
    QStringList labels;
    QList<Item> items;
    QStringList inMaintenance;
    for (int id : equipIds)
        for (const MaintenanceRecord &m : db.maintenances(def.kind, id, true))
            inMaintenance << QObject::tr("« %1 » : %2 depuis le %3 (responsable : %4)")
                                 .arg(m.equipLabel, Maintenance::operationLabel(m.kind, m.operation),
                                      m.start.toString(QStringLiteral("dd/MM/yyyy")), m.responsible);
    if (!inMaintenance.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Matériel en entretien"),
                             QObject::tr("Ce matériel est en cours d'entretien et ne peut pas être prêté :\n\n%1\n\n"
                                         "Terminez d'abord l'entretien (menu Entretien).")
                                 .arg(inMaintenance.join('\n')));
        return false;
    }
    for (int id : equipIds) {
        labels << db.label(def.kind, id);
        items.append({&def, id});
        const QVariantMap rec = db.record(def, id);
        if (rec.value(QStringLiteral("hors_service")).toInt()) {
            if (QMessageBox::question(parent, QObject::tr("Matériel hors service"),
                                      QObject::tr("« %1 » est marqué hors service. Le prêter quand même ?")
                                          .arg(def.displayLabel(rec)))
                != QMessageBox::Yes)
                return false;
        }
    }
    MemberPicker dlg(def, labels, parent);
    if (dlg.exec() != QDialog::Accepted || dlg.memberId() < 0)
        return false;
    return doBorrow(parent, dlg.memberId(), items, dlg.m_wn.when->dateTime(), dlg.m_wn.notes->text());
}

bool returnLoans(QWidget *parent, const QList<int> &loanIds)
{
    if (loanIds.isEmpty())
        return false;
    Database &db = Database::instance();
    QList<Loan> list;
    for (const Loan &l : db.loans(true))
        if (loanIds.contains(l.id))
            list << l;
    if (list.isEmpty())
        return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Retour de matériel"));
    QStringList lines;
    for (const Loan &l : list)
        lines << QStringLiteral("• %1 — emprunté par %2 le %3")
                     .arg(l.equipLabel, l.memberLabel, l.out.toString(QStringLiteral("dd/MM/yyyy")));
    auto *text = new QLabel(QObject::tr("Enregistrer le retour de :\n%1").arg(lines.join('\n')), &dlg);
    auto *form = new QFormLayout;
    WhenAndNotes wn;
    wn.addTo(form, &dlg, QObject::tr("Date de retour :"), QObject::tr("Remarque :"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Enregistrer le retour"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(text);
    l->addLayout(form);
    l->addWidget(buttons);
    wn.notes->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QStringList errors;
    db.beginBatch();
    for (const Loan &loan : list) {
        QString err;
        if (wn.when->dateTime() < loan.out) {
            errors << QObject::tr("%1 : la date de retour précède la date d'emprunt").arg(loan.equipLabel);
            continue;
        }
        if (!db.giveBack(loan.id, wn.when->dateTime(), wn.notes->text(), &err))
            errors << err;
    }
    db.endBatch();
    if (!errors.isEmpty())
        QMessageBox::warning(parent, QObject::tr("Retour"), errors.join('\n'));
    return errors.size() < list.size();
}

} // namespace LoanUi
