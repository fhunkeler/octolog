#include "deposit.h"

namespace Deposits {

QStringList methods()
{
    return {QStringLiteral("Chèque"), QStringLiteral("Espèces"), QStringLiteral("Virement")};
}

QStringList closingStatuses()
{
    return {Returned, Destroyed, Cashed};
}

QString season(const QDate &date)
{
    const int start = date.month() >= 9 ? date.year() : date.year() - 1;
    return QStringLiteral("%1-%2").arg(start).arg(start + 1);
}

QList<const EntityDef *> paidTypes(const QVariantMap &member)
{
    QList<const EntityDef *> out;
    for (const EntityDef *e : Schema::equipment())
        if (Schema::normalize(member.value(e->rightField).toString()) == QLatin1String("oui"))
            out << e;
    return out;
}

} // namespace Deposits
