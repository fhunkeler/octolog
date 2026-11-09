#include "maintenancedialogs.h"
#include "admin.h"

#include "charts.h"

#include "database.h"
#include "ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {

QDateEdit *dateEdit(QWidget *parent)
{
    auto *e = new QDateEdit(QDate::currentDate(), parent);
    e->setCalendarPopup(true);
    e->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    return e;
}

// Champs de fin d'entretien, communs aux deux fenetres.
struct FinishFields {
    QDateEdit *end;
    QComboBox *result;
    QPlainTextEdit *actions;
    QLineEdit *cost;

    void addTo(QFormLayout *form, QWidget *parent, const QString &kind)
    {
        end = dateEdit(parent);
        result = new QComboBox(parent);
        result->addItem(Ui::dot(Charts::statusGood()), Maintenance::Conform);
        result->addItem(Ui::dot(Charts::statusCritical()), Maintenance::NonConform);
        actions = new QPlainTextEdit(parent);
        actions->setTabChangesFocus(true);
        actions->setFixedHeight(actions->fontMetrics().height() * 5 + 12);
        actions->setPlaceholderText(kind == QLatin1String("blocs")
                                        ? QObject::tr("ex. : inspection visuelle intérieure et extérieure, joint de robinet changé…")
                                    : kind == QLatin1String("detendeurs")
                                        ? QObject::tr("ex. : révision 1er et 2e étages, flexible changé, kit membrane…")
                                        : QObject::tr("ex. : nettoyage, purge réparée, inflateur remplacé…"));
        cost = new QLineEdit(parent);
        auto *v = new QDoubleValidator(0, 100000, 2, cost);
        v->setNotation(QDoubleValidator::StandardNotation);
        cost->setValidator(v);
        cost->setPlaceholderText(QObject::tr("facultatif, en € par équipement"));
        form->addRow(QObject::tr("Date de fin :"), end);
        form->addRow(QObject::tr("Résultat :"), result);
        form->addRow(QObject::tr("Actions effectuées :"), actions);
        form->addRow(QObject::tr("Coût (€) :"), cost);
    }
    QVariant costValue() const
    {
        bool ok = false;
        const double c = QString(cost->text()).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        return ok ? QVariant(c) : QVariant();
    }
};

QStringList knownResponsibles()
{
    QSet<QString> seen;
    QStringList out;
    for (const MaintenanceRecord &m : Database::instance().maintenances())
        if (!m.responsible.isEmpty() && !seen.contains(m.responsible)) {
            seen.insert(m.responsible);
            out << m.responsible;
        }
    for (const QVariantMap &rec : Database::instance().records(Schema::members())) {
        if (Schema::isInactive(rec))
            continue;
        const QString name = Schema::members().displayLabel(rec);
        if (!seen.contains(name)) {
            seen.insert(name);
            out << name;
        }
    }
    return out;
}

// Materiel declare non conforme : proposer de le mettre hors service.
void offerOutOfService(QWidget *parent, const QList<QPair<QString, int>> &items)
{
    if (items.isEmpty())
        return;
    QStringList labels;
    for (const auto &[kind, id] : items)
        labels << Database::instance().label(kind, id);
    if (QMessageBox::question(parent, QObject::tr("Matériel non conforme"),
                              QObject::tr("Entretien non conforme pour :\n%1\n\nMarquer ce matériel comme hors service ?")
                                  .arg(labels.join('\n')))
        != QMessageBox::Yes)
        return;
    Database &db = Database::instance();
    db.beginBatch();
    for (const auto &[kind, id] : items)
        if (const EntityDef *def = Schema::byKind(kind))
            db.update(*def, id, {{QStringLiteral("hors_service"), 1}}, nullptr);
    db.endBatch();
}

} // namespace

namespace MaintenanceUi {

bool start(QWidget *parent, const EntityDef &def, const QList<int> &equipIds, bool alreadyDone)
{
    if (!Admin::require(parent))
        return false;
    if (equipIds.isEmpty())
        return false;
    Database &db = Database::instance();
    QStringList labels;
    for (int id : equipIds)
        labels << db.label(def.kind, id);

    QDialog dlg(parent);
    dlg.setWindowTitle(alreadyDone ? QObject::tr("Enregistrer un entretien effectué") : QObject::tr("Démarrer un entretien"));
    dlg.setWindowIcon(Ui::kindIcon(def.kind));

    auto *head = new Ui::Header(&dlg);
    head->setEmpty(def.kind, labels.size() == 1 ? labels.first() : QObject::tr("%1 %2").arg(labels.size()).arg(def.label.toLower()));
    head->setDetails(labels.size() > 1 ? labels.join(QStringLiteral(", ")).toHtmlEscaped()
                     : alreadyDone   ? QObject::tr("Entretien déjà réalisé : il est enregistré directement dans l'historique.")
                                     : QObject::tr("Pendant l'entretien, le matériel ne peut pas être emprunté."));

    auto *form = new QFormLayout;
    auto *operation = new QComboBox(&dlg);
    for (const Maintenance::Operation &op : Maintenance::operations(def.kind))
        operation->addItem(op.label, op.key);
    auto *responsible = new QComboBox(&dlg);
    responsible->setEditable(true);
    responsible->addItems(knownResponsibles());
    responsible->setCurrentText(QString());
    responsible->lineEdit()->setPlaceholderText(QObject::tr("TIV, bénévole ou prestataire"));
    responsible->completer()->setFilterMode(Qt::MatchContains);
    responsible->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    auto *startDate = dateEdit(&dlg);
    form->addRow(QObject::tr("Opération :"), operation);
    form->addRow(QObject::tr("Responsable :"), responsible);
    form->addRow(QObject::tr("Date de début :"), startDate);

    auto *done = new QCheckBox(QObject::tr("Entretien déjà terminé"), &dlg);
    done->setChecked(alreadyDone);
    auto *finishBox = new QGroupBox(QObject::tr("Fin de l'entretien"), &dlg);
    auto *finishForm = new QFormLayout(finishBox);
    FinishFields fin;
    fin.addTo(finishForm, finishBox, def.kind);
    finishBox->setVisible(alreadyDone);
    QObject::connect(done, &QCheckBox::toggled, finishBox, [finishBox, &dlg](bool on) {
        finishBox->setVisible(on);
        dlg.adjustSize();
    });
    QObject::connect(startDate, &QDateEdit::dateChanged, fin.end, [&fin](const QDate &d) {
        if (fin.end->date() < d)
            fin.end->setDate(d);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto updateOk = [&] {
        buttons->button(QDialogButtonBox::Ok)->setText(done->isChecked() ? QObject::tr("Enregistrer l'entretien")
                                                                         : QObject::tr("Démarrer l'entretien"));
    };
    updateOk();
    QObject::connect(done, &QCheckBox::toggled, &dlg, updateOk);
    buttons->button(QDialogButtonBox::Ok)->setIcon(Ui::icon(QStringLiteral("maintenance")));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        if (responsible->currentText().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, dlg.windowTitle(), QObject::tr("Indiquez la personne responsable de l'entretien."));
            responsible->setFocus();
            return;
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(head);
    l->addLayout(form);
    l->addWidget(done);
    l->addWidget(finishBox);
    l->addWidget(buttons);
    dlg.resize(520, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QStringList errors;
    QList<QPair<QString, int>> nonConform;
    int ok = 0;
    db.beginBatch();
    for (int id : equipIds) {
        MaintenanceRecord m;
        m.kind = def.kind;
        m.equipId = id;
        m.operation = operation->currentData().toString();
        m.responsible = responsible->currentText().trimmed();
        m.start = startDate->date();
        QString err;
        const int mid = db.startMaintenance(m, &err);
        if (mid < 0) {
            errors << err;
            continue;
        }
        if (done->isChecked()) {
            if (!db.finishMaintenance(mid, fin.end->date(), fin.actions->toPlainText(), fin.result->currentText(),
                                      fin.costValue(), &err)) {
                errors << err;
                db.removeMaintenance(mid, nullptr);
                continue;
            }
            if (fin.result->currentText() == Maintenance::NonConform)
                nonConform.append({def.kind, id});
        }
        ++ok;
    }
    db.endBatch();
    if (!errors.isEmpty())
        QMessageBox::warning(parent, dlg.windowTitle(), errors.join('\n'));
    offerOutOfService(parent, nonConform);
    return ok > 0;
}

bool finish(QWidget *parent, const QList<int> &maintenanceIds)
{
    if (!Admin::require(parent))
        return false;
    Database &db = Database::instance();
    QList<MaintenanceRecord> list;
    for (const MaintenanceRecord &m : db.maintenances({}, -1, true))
        if (maintenanceIds.contains(m.id))
            list << m;
    if (list.isEmpty())
        return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Terminer l'entretien"));
    QStringList lines;
    for (const MaintenanceRecord &m : list)
        lines << QObject::tr("%1 — %2, depuis le %3 (%4)")
                     .arg(m.equipLabel.toHtmlEscaped(), Maintenance::operationLabel(m.kind, m.operation).toHtmlEscaped(),
                          m.start.toString(QStringLiteral("dd/MM/yyyy")), m.responsible.toHtmlEscaped());
    auto *head = new Ui::Header(&dlg);
    head->setEmpty(list.first().kind, list.size() == 1 ? list.first().equipLabel
                                                       : QObject::tr("%1 entretiens").arg(list.size()));
    head->setDetails(lines.join(QStringLiteral("<br>")));

    auto *form = new QFormLayout;
    FinishFields fin;
    fin.addTo(form, &dlg, list.first().kind);
    QDate minStart = list.first().start;
    for (const MaintenanceRecord &m : list)
        minStart = qMax(minStart, m.start);
    fin.end->setMinimumDate(minStart);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Terminer l'entretien"));
    buttons->button(QDialogButtonBox::Ok)->setIcon(Ui::icon(QStringLiteral("maintenance")));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(head);
    l->addLayout(form);
    l->addWidget(buttons);
    dlg.resize(560, dlg.sizeHint().height());
    fin.actions->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QStringList errors;
    QList<QPair<QString, int>> nonConform;
    db.beginBatch();
    for (const MaintenanceRecord &m : list) {
        QString err;
        if (!db.finishMaintenance(m.id, fin.end->date(), fin.actions->toPlainText(), fin.result->currentText(),
                                  fin.costValue(), &err))
            errors << err;
        else if (fin.result->currentText() == Maintenance::NonConform)
            nonConform.append({m.kind, m.equipId});
    }
    db.endBatch();
    if (!errors.isEmpty())
        QMessageBox::warning(parent, dlg.windowTitle(), errors.join('\n'));
    offerOutOfService(parent, nonConform);
    return errors.size() < list.size();
}

} // namespace MaintenanceUi
