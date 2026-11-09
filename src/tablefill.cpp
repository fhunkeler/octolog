#include "tablefill.h"

#include "admin.h"

#include "charts.h"

#include "searchabletable.h"
#include "searchproxy.h"
#include "ui.h"

#include <QApplication>
#include <QPalette>

namespace TableFill {

QString durationText(int days)
{
    if (days <= 0)
        return QStringLiteral("aujourd'hui");
    return days == 1 ? QStringLiteral("1 jour") : QStringLiteral("%1 jours").arg(days);
}

static QString dateTimeText(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QString();
}

void entities(SearchableTable *table, const EntityDef &def, const LoanIndex &index)
{
    QStringList headers;
    QList<bool> visible;
    for (const Field &f : def.fields) {
        headers << f.label;
        visible << f.inTable;
    }
    // Donnees reservees au mode admin (entretien, cautions) : absentes du modele en mode utilisateur,
    // donc ni affichees, ni cherchables, ni exportables.
    const bool admin = Admin::instance().isAdmin();
    if (def.isEquipment) {
        headers << QStringLiteral("Disponibilité") << QStringLiteral("Emprunté par") << QStringLiteral("Emprunté le");
        visible << true << true << false;
        if (admin) {
            headers << QStringLiteral("Entretien") << QStringLiteral("Prochaine échéance");
            visible << true << false;
        }
    } else {
        headers << QStringLiteral("Droits de prêt") << QStringLiteral("Matériel emprunté");
        visible << true << true;
        if (admin) {
            headers << QStringLiteral("Caution");
            visible << true;
        }
    }

    table->beginReset();
    table->setHeaders(headers, visible);
    const QColor muted = QApplication::palette().color(QPalette::Disabled, QPalette::Text);

    const QList<QVariantMap> recs = Database::instance().records(def);
    for (const QVariantMap &rec : recs) {
        const int id = rec.value(QStringLiteral("id")).toInt();
        QList<QStandardItem *> items;
        for (const Field &f : def.fields) {
            const QVariant v = rec.value(f.key);
            items << makeItem(Schema::formatValue(f, v), Schema::sortValue(f, v));
        }
        QString status;
        if (def.isEquipment) {
            const auto loans = index.byEquipment.value(def.kind);
            const auto open = index.inMaintenance.value(def.kind);
            const bool hs = rec.value(QStringLiteral("hors_service")).toInt() != 0;
            if (loans.contains(id)) {
                const Loan l = loans.value(id);
                status = Borrowed;
                items << makeItem(status) << makeItem(l.memberLabel)
                      << makeItem(dateTimeText(l.out), l.out);
            } else if (open.contains(id)) {
                const MaintenanceRecord m = open.value(id);
                status = InMaintenance;
                items << makeItem(status) << makeItem(QStringLiteral("(entretien : %1)").arg(m.responsible))
                      << makeItem({});
            } else {
                status = hs ? OutOfService : Available;
                items << makeItem(status) << makeItem({}) << makeItem({});
            }
            QStandardItem *statusItem = items.at(def.fields.size());
            statusItem->setIcon(Ui::dot(status == Borrowed        ? Charts::borrowed()
                                        : status == InMaintenance ? Charts::inMaintenance()
                                        : status == OutOfService  ? Charts::outOfService()
                                                                  : Charts::available()));

            // Entretien : etat le plus defavorable et prochaine echeance (mode admin)
            const QList<Maintenance::Due> dues =
                Maintenance::dues(def, rec, index.journal.value(def.kind).value(id));
            const Maintenance::State st = Maintenance::worst(dues);
            const Maintenance::Due *next = Maintenance::nextDue(dues);
            QStringList tips;
            for (const Maintenance::Due &d : dues)
                tips << QStringLiteral("%1 : %2").arg(d.op.label, Maintenance::dueText(d));
            // Sans obligation (gilets) : date du dernier entretien enregistre
            QDate lastDone;
            for (const Maintenance::Due &d : dues)
                if (d.last > lastDone)
                    lastDone = d.last;
            for (const MaintenanceRecord &m : index.journal.value(def.kind).value(id))
                if (m.end > lastDone)
                    lastDone = m.end;
            const QString maintText = st != Maintenance::State::None ? Maintenance::stateText(st)
                                      : lastDone.isValid() ? QStringLiteral("Dernier : %1").arg(lastDone.toString(QStringLiteral("dd/MM/yyyy")))
                                                           : QStringLiteral("Jamais entretenu");
            QStandardItem *maint = makeItem(maintText, st != Maintenance::State::None ? QVariant(int(st)) : QVariant(lastDone));
            maint->setIcon(Ui::dot(Maintenance::stateColor(st)));
            maint->setToolTip(tips.join('\n'));
            if (admin) {
                items << maint;
                items << (next && next->next.isValid()
                              ? makeItem(QStringLiteral("%1 — %2").arg(next->next.toString(QStringLiteral("dd/MM/yyyy")), next->op.label),
                                         next->next)
                              : makeItem({}));
                items.first()->setData(int(st), Roles::Maintenance);
            } else {
                delete maint;
            }
            if (status == OutOfService)
                for (QStandardItem *it : items)
                    it->setForeground(muted);
        } else {
            const QList<Loan> loans = index.byMember.value(id);
            QStringList labels;
            for (const Loan &l : loans)
                labels << l.equipLabel;
            status = QString::number(loans.size());
            QStringList rights;
            for (const EntityDef *e : Schema::equipment())
                if (Schema::isYes(rec.value(e->rightField)))
                    rights << e->singular;
            items << makeItem(rights.join(QStringLiteral(", ")))
                  << makeItem(labels.join(QStringLiteral(", ")));
            if (!loans.isEmpty())
                items.last()->setIcon(Ui::dot(Charts::borrowed()));
            // Caution : seulement pour les prets payants (droit "Oui")
            QStandardItem *deposit;
            if (index.heldDeposit.contains(id)) {
                const Deposit d = index.heldDeposit.value(id);
                deposit = makeItem(QStringLiteral("Reçue (%1 €)").arg(d.amount), 2);
                deposit->setIcon(Ui::dot(Charts::statusGood()));
                deposit->setToolTip(QStringLiteral("%1 %2 reçu le %3, saison %4")
                                        .arg(d.method, d.number, d.received.toString(QStringLiteral("dd/MM/yyyy")), d.season));
            } else if (Deposits::needsDeposit(rec) && !Schema::isInactive(rec)) {
                deposit = makeItem(QStringLiteral("Manquante"), 0);
                deposit->setIcon(Ui::dot(loans.isEmpty() ? Charts::statusWarning() : Charts::statusCritical()));
            } else {
                deposit = makeItem(QString(), 1);
            }
            if (admin)
                items << deposit;
            else
                delete deposit;
            const QDate end = QDate::fromString(rec.value(QStringLiteral("adhesion_fin")).toString(), Qt::ISODate);
            if ((end.isValid() && end < QDate::currentDate()) || Schema::isInactive(rec))
                for (QStandardItem *it : items)
                    it->setForeground(muted);
            if (Schema::isInactive(rec))
                for (QStandardItem *it : items) {
                    QFont f = it->font();
                    f.setItalic(true);
                    it->setFont(f);
                }
        }
        QVariantMap recWithMeta = rec;
        if (!def.isEquipment && admin)
            recWithMeta.insert(QStringLiteral("__caution"), index.heldDeposit.contains(id));
        items.first()->setData(recWithMeta, Roles::Record);
        items.first()->setData(status, Roles::Status);
        table->appendRow(id, items);
    }
    table->endReset();
}

QStringList adminFilters(const EntityDef &def)
{
    if (def.isEquipment)
        return {QStringLiteral("Entretien en retard"), QStringLiteral("Entretien à prévoir (30 j)")};
    return {QStringLiteral("Actifs avec prêt payant sans caution")};
}

void addStandardFilters(SearchableTable *table, const EntityDef &def)
{
    table->addStatusFilter(QStringLiteral("Tous"), {});
    if (def.isEquipment) {
        for (const QString &s : {Available, Borrowed, InMaintenance, OutOfService})
            table->addStatusFilter(s == Available ? QStringLiteral("Disponibles")
                                   : s == Borrowed ? QStringLiteral("Empruntés")
                                                   : s,
                                   [s](const QStandardItem *it) { return it->data(Roles::Status).toString() == s; });
        table->addStatusFilter(QStringLiteral("Entretien en retard"), [](const QStandardItem *it) {
            return it->data(Roles::Maintenance).toInt() == int(Maintenance::State::Overdue);
        });
        table->addStatusFilter(QStringLiteral("Entretien à prévoir (30 j)"), [](const QStandardItem *it) {
            return it->data(Roles::Maintenance).toInt() == int(Maintenance::State::Soon);
        });
        for (const QString &f : adminFilters(def))
            table->setStatusFilterVisible(f, Admin::instance().isAdmin());
        return;
    }
    // Adherents : les filtres (sauf "Tous" et "Desactives") ne montrent que les actifs.
    auto rec = [](const QStandardItem *it) { return it->data(Roles::Record).toMap(); };
    auto active = [rec](const QStandardItem *it) { return !Schema::isInactive(rec(it)); };
    table->addStatusFilter(MembersActive, active);
    table->addStatusFilter(QStringLiteral("Actifs avec emprunt en cours"), [active](const QStandardItem *it) {
        return active(it) && it->data(Roles::Status).toInt() > 0;
    });
    table->addStatusFilter(QStringLiteral("Actifs sans emprunt"), [active](const QStandardItem *it) {
        return active(it) && it->data(Roles::Status).toInt() == 0;
    });
    for (const EntityDef *e : Schema::equipment()) {
        const QString right = e->rightField;
        table->addStatusFilter(rightFilterLabel(*e), [active, rec, right](const QStandardItem *it) {
            return active(it) && Schema::isYes(rec(it).value(right));
        });
    }
    table->addStatusFilter(QStringLiteral("Actifs avec prêt payant sans caution"), [active, rec](const QStandardItem *it) {
        return active(it) && Deposits::needsDeposit(rec(it)) && !rec(it).value(QStringLiteral("__caution")).toBool();
    });
    table->addStatusFilter(QStringLiteral("Adhésion expirée"), [active, rec](const QStandardItem *it) {
        const QDate end = QDate::fromString(rec(it).value("adhesion_fin").toString(), Qt::ISODate);
        return active(it) && end.isValid() && end < QDate::currentDate();
    });
    table->addStatusFilter(QStringLiteral("Désactivés (absents du dernier import)"), [active](const QStandardItem *it) {
        return !active(it);
    });
    for (const QString &f : adminFilters(def))
        table->setStatusFilterVisible(f, Admin::instance().isAdmin());
    table->setStatusFilter(1); // actifs par defaut
}

QString rightFilterLabel(const EntityDef &equipment)
{
    return QStringLiteral("Actifs avec droit de prêt : %1").arg(equipment.singular);
}

void loans(SearchableTable *table, const QList<Loan> &list, int columns)
{
    QStringList headers;
    if (columns & WithMember)
        headers << QStringLiteral("Adhérent");
    if (columns & WithContact)
        headers << QStringLiteral("Email") << QStringLiteral("Téléphone");
    if (columns & WithEquipment)
        headers << QStringLiteral("Type") << QStringLiteral("Matériel");
    headers << QStringLiteral("Date emprunt");
    if (columns & WithReturn)
        headers << QStringLiteral("Date retour");
    headers << QStringLiteral("Durée (jours)");
    if (columns & WithReturn)
        headers << QStringLiteral("Statut");
    headers << QStringLiteral("Notes emprunt");
    if (columns & WithReturn)
        headers << QStringLiteral("Notes retour");

    QHash<int, QVariantMap> members;
    if (columns & WithContact)
        for (const QVariantMap &m : Database::instance().records(Schema::members()))
            members.insert(m.value(QStringLiteral("id")).toInt(), m);

    table->beginReset();
    table->setHeaders(headers);
    for (const Loan &l : list) {
        QList<QStandardItem *> items;
        if (columns & WithMember)
            items << makeItem(l.memberLabel);
        if (columns & WithContact) {
            const QVariantMap m = members.value(l.memberId);
            items << makeItem(m.value(QStringLiteral("email")).toString())
                  << makeItem(m.value(QStringLiteral("telephone")).toString());
        }
        if (columns & WithEquipment) {
            const EntityDef *def = Schema::byKind(l.kind);
            items << makeItem(def ? def->label : l.kind) << makeItem(l.equipLabel);
            items.at(items.size() - 2)->setIcon(Ui::kindIcon(l.kind));
        }
        items << makeItem(dateTimeText(l.out), l.out);
        if (columns & WithReturn)
            items << makeItem(dateTimeText(l.back), l.back.isValid() ? QVariant(l.back) : QVariant());
        items << makeItem(QString::number(l.durationDays()), double(l.durationDays()));
        if (columns & WithReturn)
            items << makeItem(l.active() ? QStringLiteral("En cours") : QStringLiteral("Rendu"));
        items << makeItem(l.notesOut);
        if (columns & WithReturn)
            items << makeItem(l.notesBack);
        if (l.active() && (columns & WithReturn)) {
            for (QStandardItem *it : items) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
        }
        QVariantMap meta{{"memberId", l.memberId}, {"kind", l.kind}, {"equipId", l.equipId},
                         {"active", l.active()}};
        items.first()->setData(meta, Roles::Record);
        items.first()->setData(l.active() ? QStringLiteral("active") : QStringLiteral("done"), Roles::Status);
        table->appendRow(l.id, items);
    }
    table->endReset();
}

} // namespace TableFill
