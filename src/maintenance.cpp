#include "maintenance.h"

#include "charts.h"

#include "ui.h"

namespace Maintenance {

QList<Operation> operations(const QString &kind)
{
    QList<Operation> ops;
    if (kind == QLatin1String("blocs")) {
        ops = {{QStringLiteral("inspection"), QStringLiteral("Inspection périodique (TIV)"), QStringLiteral("date_inspection"), true},
               {QStringLiteral("requalification"), QStringLiteral("Requalification"), QStringLiteral("date_requalif"), true}};
    } else if (kind == QLatin1String("detendeurs")) {
        ops = {{QStringLiteral("revision"), QStringLiteral("Révision annuelle"), QStringLiteral("date_revision"), true}};
    } else if (kind == QLatin1String("gilets")) {
        ops = {{QStringLiteral("verification"), QStringLiteral("Vérification"), QStringLiteral("date_verif"), false},
               {QStringLiteral("nettoyage"), QStringLiteral("Nettoyage / désinfection"), QString(), false},
               {QStringLiteral("reparation"), QStringLiteral("Réparation"), QString(), false}};
    }
    ops.append({Other, QStringLiteral("Autre intervention"), QString(), false});
    return ops;
}

QString operationLabel(const QString &kind, const QString &key)
{
    for (const Operation &op : operations(kind))
        if (op.key == key)
            return op.label;
    return key;
}

QString blocRegime(const QVariantMap &rec)
{
    if (Schema::normalize(rec.value(QStringLiteral("materiau")).toString()).startsWith(QLatin1String("composite")))
        return Standard;
    const QString r = rec.value(QStringLiteral("regime")).toString();
    return Schema::normalize(r).startsWith(QLatin1String("standard")) ? Standard : Derogatory;
}

static int periodMonths(const QString &kind, const QString &op, const QVariantMap &rec)
{
    if (kind == QLatin1String("blocs")) {
        if (op == QLatin1String("inspection"))
            return 12;
        if (op == QLatin1String("requalification"))
            return blocRegime(rec) == Standard ? 24 : 72;
    }
    if (kind == QLatin1String("detendeurs") && op == QLatin1String("revision"))
        return 12;
    return 0;
}

QList<Due> dues(const EntityDef &def, const QVariantMap &rec, const QList<MaintenanceRecord> &journal, const QDate &today)
{
    QList<Due> out;
    for (const Operation &op : operations(def.kind)) {
        if (op.dateField.isEmpty())
            continue;
        Due d;
        d.op = op;
        d.months = periodMonths(def.kind, op.key, rec);
        d.last = QDate::fromString(rec.value(op.dateField).toString(), Qt::ISODate);
        for (const MaintenanceRecord &m : journal) {
            // Une requalification vaut aussi inspection periodique.
            const bool counts = m.operation == op.key
                                || (op.key == QLatin1String("inspection") && m.operation == QLatin1String("requalification"));
            if (counts && !m.open() && m.conform() && m.end > d.last)
                d.last = m.end;
        }
        if (d.months > 0) {
            if (d.last.isValid()) {
                d.next = d.last.addMonths(d.months);
                d.daysLeft = int(today.daysTo(d.next));
                d.state = d.daysLeft < 0 ? State::Overdue : d.daysLeft <= SoonDays ? State::Soon : State::Ok;
            } else {
                d.state = State::Overdue; // jamais fait (ou date inconnue)
            }
        }
        out << d;
    }
    return out;
}

State worst(const QList<Due> &dues)
{
    State w = State::None;
    for (const Due &d : dues)
        if (int(d.state) > int(w))
            w = d.state;
    return w;
}

const Due *nextDue(const QList<Due> &dues)
{
    const Due *best = nullptr;
    for (const Due &d : dues) {
        if (d.months <= 0)
            continue;
        if (!best || (!d.next.isValid() && best->next.isValid()) || (d.next.isValid() && best->next.isValid() && d.next < best->next))
            best = &d;
    }
    return best;
}

QString stateText(State s)
{
    switch (s) {
    case State::Ok:
        return QStringLiteral("À jour");
    case State::Soon:
        return QStringLiteral("À prévoir");
    case State::Overdue:
        return QStringLiteral("En retard");
    default:
        return QStringLiteral("Sans obligation");
    }
}

QColor stateColor(State s)
{
    switch (s) {
    case State::Ok:
        return Charts::statusGood();
    case State::Soon:
        return Charts::statusWarning();
    case State::Overdue:
        return Charts::statusCritical();
    default:
        return Charts::outOfService();
    }
}

QString dueText(const Due &d)
{
    if (d.months <= 0)
        return d.last.isValid() ? QStringLiteral("Dernière le %1").arg(d.last.toString(QStringLiteral("dd/MM/yyyy")))
                                : QStringLiteral("Jamais enregistrée");
    if (!d.next.isValid())
        return QStringLiteral("Date inconnue : à faire");
    const QString date = d.next.toString(QStringLiteral("dd/MM/yyyy"));
    if (d.daysLeft < 0)
        return QStringLiteral("En retard depuis le %1 (%2 j)").arg(date).arg(-d.daysLeft);
    if (d.daysLeft <= SoonDays)
        return QStringLiteral("Avant le %1 (dans %2 j)").arg(date).arg(d.daysLeft);
    return QStringLiteral("Avant le %1").arg(date);
}

} // namespace Maintenance
