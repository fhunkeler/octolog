#include "depositdialogs.h"
#include "admin.h"

#include "database.h"
#include "ui.h"

#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QDateEdit *dateEdit(QWidget *parent, const QDate &d = QDate::currentDate())
{
    auto *e = new QDateEdit(d, parent);
    e->setCalendarPopup(true);
    e->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    return e;
}

QString paidText(const QVariantMap &member)
{
    QStringList types;
    for (const EntityDef *e : Deposits::paidTypes(member))
        types << e->singular;
    return types.isEmpty() ? QObject::tr("aucun prêt payant") : QObject::tr("prêt payant : %1").arg(types.join(QStringLiteral(", ")));
}

} // namespace

namespace DepositUi {

bool record(QWidget *parent, int memberId, const Deposit *existing)
{
    if (!Admin::require(parent))
        return false;
    Database &db = Database::instance();
    const EntityDef &mdef = Schema::members();
    if (existing)
        memberId = existing->memberId;

    QDialog dlg(parent);
    dlg.setWindowTitle(existing ? QObject::tr("Modifier la caution") : QObject::tr("Enregistrer une caution"));
    dlg.setWindowIcon(Ui::icon(QStringLiteral("deposit")));

    auto *head = new Ui::Header(&dlg);
    auto *member = new QComboBox(&dlg);
    // Adherents actifs, ceux qui ont un pret payant en premier
    QList<QVariantMap> recs = db.records(mdef);
    std::stable_sort(recs.begin(), recs.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const bool pa = Deposits::needsDeposit(a), pb = Deposits::needsDeposit(b);
        if (pa != pb)
            return pa;
        return QString::localeAwareCompare(mdef.displayLabel(a), mdef.displayLabel(b)) < 0;
    });
    for (const QVariantMap &r : recs) {
        if (Schema::isInactive(r) && r.value("id").toInt() != memberId)
            continue;
        member->addItem(QStringLiteral("%1 — %2").arg(mdef.displayLabel(r), paidText(r)), r.value("id"));
    }
    member->setEditable(true);
    member->setInsertPolicy(QComboBox::NoInsert);
    member->completer()->setFilterMode(Qt::MatchContains);
    member->completer()->setCompletionMode(QCompleter::PopupCompletion);
    if (memberId >= 0) {
        member->setCurrentIndex(member->findData(memberId));
        member->setEnabled(false);
    } else {
        member->setCurrentIndex(-1);
        member->lineEdit()->setPlaceholderText(QObject::tr("tapez un nom…"));
    }
    auto updateHead = [&] {
        const int id = member->currentData().toInt();
        const QVariantMap rec = db.record(mdef, id);
        if (rec.isEmpty()) {
            head->setEmpty(QStringLiteral("membres"), QObject::tr("Choisissez l'adhérent"));
            head->setDetails(QObject::tr("Une caution couvre tous les prêts payants de l'adhérent pour la saison."));
            return;
        }
        head->setRecord(mdef, rec, int(db.loans(true, id).size()));
        QString details = paidText(rec);
        for (const Deposit &d : db.deposits(id, true))
            if (!existing || d.id != existing->id)
                details += QObject::tr("<br><b>Caution déjà détenue</b> : %1 € reçue le %2 (saison %3)")
                               .arg(d.amount)
                               .arg(d.received.toString(QStringLiteral("dd/MM/yyyy")), d.season);
        head->setDetails(details);
    };
    QObject::connect(member, &QComboBox::currentIndexChanged, &dlg, updateHead);

    QSettings settings;
    auto *season = new QComboBox(&dlg);
    season->setEditable(true);
    const QString current = Deposits::season(QDate::currentDate());
    const int y = QDate::currentDate().month() >= 9 ? QDate::currentDate().year() : QDate::currentDate().year() - 1;
    season->addItems({QStringLiteral("%1-%2").arg(y - 1).arg(y), current, QStringLiteral("%1-%2").arg(y + 1).arg(y + 2)});
    season->setCurrentText(existing ? existing->season : current);
    auto *amount = new QDoubleSpinBox(&dlg);
    amount->setRange(0, 10000);
    amount->setDecimals(2);
    amount->setSuffix(QStringLiteral(" €"));
    amount->setValue(existing ? existing->amount : settings.value(QStringLiteral("cautions/montant"), 0.0).toDouble());
    auto *method = new QComboBox(&dlg);
    method->addItems(Deposits::methods());
    if (existing)
        method->setCurrentText(existing->method);
    auto *number = new QLineEdit(existing ? existing->number : QString(), &dlg);
    number->setPlaceholderText(QObject::tr("n° du chèque"));
    auto *bank = new QLineEdit(existing ? existing->bank : QString(), &dlg);
    {
        QSet<QString> banks;
        for (const Deposit &d : db.deposits())
            if (!d.bank.isEmpty())
                banks.insert(d.bank);
        auto *c = new QCompleter(QStringList(banks.begin(), banks.end()), bank);
        c->setCaseSensitivity(Qt::CaseInsensitive);
        c->setFilterMode(Qt::MatchContains);
        bank->setCompleter(c);
    }
    auto *received = dateEdit(&dlg, existing ? existing->received : QDate::currentDate());
    auto *note = new QLineEdit(existing ? existing->note : QString(), &dlg);
    note->setPlaceholderText(QObject::tr("facultatif"));
    auto toggleCheque = [&] {
        const bool cheque = method->currentText() == QLatin1String("Chèque");
        number->setEnabled(cheque);
        bank->setEnabled(cheque);
    };
    QObject::connect(method, &QComboBox::currentTextChanged, &dlg, toggleCheque);
    toggleCheque();

    auto *form = new QFormLayout;
    form->addRow(QObject::tr("Adhérent :"), member);
    form->addRow(QObject::tr("Saison :"), season);
    form->addRow(QObject::tr("Montant :"), amount);
    form->addRow(QObject::tr("Moyen :"), method);
    form->addRow(QObject::tr("N° de chèque :"), number);
    form->addRow(QObject::tr("Banque :"), bank);
    form->addRow(QObject::tr("Reçue le :"), received);
    form->addRow(QObject::tr("Remarque :"), note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(existing ? QObject::tr("Enregistrer") : QObject::tr("Enregistrer la caution"));
    buttons->button(QDialogButtonBox::Ok)->setIcon(Ui::icon(QStringLiteral("deposit")));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        if (member->currentIndex() < 0 || member->currentData().toInt() <= 0) {
            QMessageBox::warning(&dlg, dlg.windowTitle(), QObject::tr("Choisissez l'adhérent."));
            member->setFocus();
            return;
        }
        if (amount->value() <= 0) {
            QMessageBox::warning(&dlg, dlg.windowTitle(), QObject::tr("Indiquez le montant de la caution."));
            amount->setFocus();
            return;
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(head);
    l->addLayout(form);
    l->addWidget(buttons);
    updateHead();
    dlg.resize(520, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted)
        return false;

    Deposit d = existing ? *existing : Deposit();
    d.memberId = member->currentData().toInt();
    d.season = season->currentText().trimmed();
    d.amount = amount->value();
    d.method = method->currentText();
    d.number = number->isEnabled() ? number->text() : QString();
    d.bank = bank->isEnabled() ? bank->text() : QString();
    d.received = received->date();
    d.note = note->text();
    settings.setValue(QStringLiteral("cautions/montant"), d.amount);
    QString err;
    const bool ok = existing ? db.updateDeposit(d, &err) : db.addDeposit(d, &err) > 0;
    if (!ok)
        QMessageBox::warning(parent, dlg.windowTitle(), err);
    return ok;
}

bool close(QWidget *parent, const QList<int> &depositIds)
{
    if (!Admin::require(parent))
        return false;
    Database &db = Database::instance();
    QList<Deposit> list;
    for (const Deposit &d : db.deposits(-1, true))
        if (depositIds.contains(d.id))
            list << d;
    if (list.isEmpty())
        return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Clôturer la caution"));
    QStringList lines;
    double total = 0;
    for (const Deposit &d : list) {
        lines << QObject::tr("%1 — %2 € (%3%4), saison %5")
                     .arg(d.memberLabel.toHtmlEscaped())
                     .arg(d.amount)
                     .arg(d.method.toHtmlEscaped(), d.number.isEmpty() ? QString() : QStringLiteral(" n° ") + d.number.toHtmlEscaped())
                     .arg(d.season);
        total += d.amount;
    }
    auto *head = new Ui::Header(&dlg);
    head->setEmpty(QStringLiteral("deposit"), list.size() == 1 ? list.first().memberLabel
                                                               : QObject::tr("%1 cautions (%2 €)").arg(list.size()).arg(total));
    head->setDetails(lines.join(QStringLiteral("<br>")));

    auto *status = new QComboBox(&dlg);
    status->addItems(Deposits::closingStatuses());
    status->setCurrentText(Deposits::Destroyed);
    auto *date = dateEdit(&dlg);
    auto *note = new QLineEdit(&dlg);
    note->setPlaceholderText(QObject::tr("facultatif"));
    auto *form = new QFormLayout;
    form->addRow(QObject::tr("Devenir de la caution :"), status);
    form->addRow(QObject::tr("Date :"), date);
    form->addRow(QObject::tr("Remarque :"), note);

    // Avertir si l'adherent a encore du materiel
    QStringList stillBorrowing;
    for (const Deposit &d : list)
        if (!db.loans(true, d.memberId).isEmpty())
            stillBorrowing << d.memberLabel;
    auto *warning = new QLabel(&dlg);
    warning->setWordWrap(true);
    warning->setVisible(!stillBorrowing.isEmpty());
    warning->setText(QObject::tr("<b>Attention</b> : %1 a encore du matériel emprunté.").arg(stillBorrowing.join(QStringLiteral(", "))));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Clôturer"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(head);
    l->addWidget(warning);
    l->addLayout(form);
    l->addWidget(buttons);
    dlg.resize(520, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QStringList errors;
    db.beginBatch();
    for (const Deposit &d : list) {
        QString err;
        if (!db.closeDeposit(d.id, status->currentText(), date->date(), note->text(), &err))
            errors << err;
    }
    db.endBatch();
    if (!errors.isEmpty())
        QMessageBox::warning(parent, dlg.windowTitle(), errors.join('\n'));
    return errors.size() < list.size();
}

} // namespace DepositUi
