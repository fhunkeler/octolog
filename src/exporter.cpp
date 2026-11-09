#include "exporter.h"

#include "database.h"
#include "maintenance.h"
#include "xlsx.h"

namespace {

// Valeur typee pour Excel : dates et nombres restent exploitables (tri, filtres, formules).
QVariant cell(const Field &f, const QVariant &v)
{
    if (!v.isValid() || v.isNull() || v.toString().isEmpty())
        return {};
    switch (f.type) {
    case FieldType::Date: {
        const QDate d = QDate::fromString(v.toString(), Qt::ISODate);
        return d.isValid() ? QVariant(d) : v;
    }
    case FieldType::Integer:
    case FieldType::Real: {
        bool ok = false;
        const double x = v.toDouble(&ok);
        return ok ? QVariant(x) : v;
    }
    case FieldType::Bool:
        return v.toInt() ? QStringLiteral("Oui") : QString();
    default:
        return v.toString();
    }
}

QVariant dateCell(const QDate &d)
{
    return d.isValid() ? QVariant(d) : QVariant();
}

QVariant dateTimeCell(const QDateTime &d)
{
    return d.isValid() ? QVariant(d) : QVariant();
}

} // namespace

namespace Exporter {

bool exportAll(const QString &path, QString *error, QStringList *summary, bool includeAdmin)
{
    Database &db = Database::instance();
    const LoanIndex idx = db.activeLoanIndex();
    QList<Xlsx::SheetData> sheets;
    QHash<int, QVariantMap> members;

    // Adherents
    {
        const EntityDef &def = Schema::members();
        Xlsx::SheetData sh{def.label, {}, {}};
        for (const Field &f : def.fields)
            sh.headers << f.label;
        sh.headers << QStringLiteral("Emprunts en cours") << QStringLiteral("Matériel emprunté");
        for (const QVariantMap &rec : db.records(def)) {
            const int id = rec.value(QStringLiteral("id")).toInt();
            members.insert(id, rec);
            QVariantList row;
            for (const Field &f : def.fields)
                row << cell(f, rec.value(f.key));
            const QList<Loan> loans = idx.byMember.value(id);
            QStringList labels;
            for (const Loan &l : loans)
                labels << l.equipLabel;
            row << double(loans.size()) << labels.join(QStringLiteral(", "));
            sh.rows << row;
        }
        sheets << sh;
    }

    // Materiel
    for (const EntityDef *def : Schema::equipment()) {
        Xlsx::SheetData sh{def->label, {}, {}};
        for (const Field &f : def->fields)
            sh.headers << f.label;
        sh.headers << QStringLiteral("Disponibilité") << QStringLiteral("Emprunté par") << QStringLiteral("Emprunté le");
        if (includeAdmin) {
            sh.headers << QStringLiteral("État de l'entretien") << QStringLiteral("Prochaine échéance")
                       << QStringLiteral("Opération à prévoir");
            if (def->kind == QLatin1String("blocs"))
                sh.headers << QStringLiteral("Régime appliqué");
        }
        for (const QVariantMap &rec : db.records(*def)) {
            const int id = rec.value(QStringLiteral("id")).toInt();
            QVariantList row;
            for (const Field &f : def->fields)
                row << cell(f, rec.value(f.key));
            const bool borrowed = idx.byEquipment.value(def->kind).contains(id);
            const bool inMaint = idx.inMaintenance.value(def->kind).contains(id);
            const Loan loan = idx.byEquipment.value(def->kind).value(id);
            row << (borrowed ? QStringLiteral("Emprunté")
                    : inMaint ? QStringLiteral("En entretien")
                    : rec.value(QStringLiteral("hors_service")).toInt() ? QStringLiteral("Hors service")
                                                                        : QStringLiteral("Disponible"))
                << (borrowed ? loan.memberLabel : QString()) << (borrowed ? dateTimeCell(loan.out) : QVariant());
            if (includeAdmin) {
                const QList<Maintenance::Due> dues = Maintenance::dues(*def, rec, idx.journal.value(def->kind).value(id));
                const Maintenance::Due *next = Maintenance::nextDue(dues);
                row << Maintenance::stateText(Maintenance::worst(dues))
                    << (next ? dateCell(next->next) : QVariant()) << (next ? next->op.label : QString());
                if (def->kind == QLatin1String("blocs"))
                    row << Maintenance::blocRegime(rec);
            }
            sh.rows << row;
        }
        sheets << sh;
    }

    // Emprunts
    const QList<Loan> allLoans = db.loans(false);
    {
        Xlsx::SheetData current{QStringLiteral("Emprunts en cours"),
                                {QStringLiteral("Adhérent"), QStringLiteral("Email"), QStringLiteral("Téléphone"),
                                 QStringLiteral("Type"), QStringLiteral("Matériel"), QStringLiteral("Date emprunt"),
                                 QStringLiteral("Durée (jours)"), QStringLiteral("Notes emprunt")},
                                {}};
        Xlsx::SheetData history{QStringLiteral("Historique des emprunts"),
                                {QStringLiteral("Adhérent"), QStringLiteral("Email"), QStringLiteral("Type"),
                                 QStringLiteral("Matériel"), QStringLiteral("Date emprunt"), QStringLiteral("Date retour"),
                                 QStringLiteral("Durée (jours)"), QStringLiteral("Statut"), QStringLiteral("Notes emprunt"),
                                 QStringLiteral("Notes retour")},
                                {}};
        for (const Loan &l : allLoans) {
            const QVariantMap m = members.value(l.memberId);
            const EntityDef *def = Schema::byKind(l.kind);
            const QString type = def ? def->label : l.kind;
            if (l.active())
                current.rows << QVariantList{l.memberLabel, m.value(QStringLiteral("email")).toString(),
                                             m.value(QStringLiteral("telephone")).toString(), type, l.equipLabel,
                                             dateTimeCell(l.out), double(l.durationDays()), l.notesOut};
            history.rows << QVariantList{l.memberLabel, m.value(QStringLiteral("email")).toString(), type, l.equipLabel,
                                         dateTimeCell(l.out), dateTimeCell(l.back), double(l.durationDays()),
                                         l.active() ? QStringLiteral("En cours") : QStringLiteral("Terminé"), l.notesOut,
                                         l.notesBack};
        }
        sheets << current << history;
    }

    if (includeAdmin) {
        // Entretiens
        {
            Xlsx::SheetData sh{QStringLiteral("Entretiens"),
                               {QStringLiteral("Type"), QStringLiteral("Matériel"), QStringLiteral("Opération"),
                                QStringLiteral("Responsable"), QStringLiteral("Début"), QStringLiteral("Fin"),
                                QStringLiteral("Statut"), QStringLiteral("Résultat"), QStringLiteral("Coût (€)"),
                                QStringLiteral("Actions effectuées")},
                               {}};
            for (const MaintenanceRecord &m : db.maintenances()) {
                const EntityDef *def = Schema::byKind(m.kind);
                sh.rows << QVariantList{def ? def->label : m.kind, m.equipLabel, Maintenance::operationLabel(m.kind, m.operation),
                                        m.responsible, dateCell(m.start), dateCell(m.end),
                                        m.open() ? QStringLiteral("En cours") : QStringLiteral("Terminé"),
                                        m.open() ? QString() : m.result,
                                        m.cost.isValid() && !m.cost.toString().isEmpty() ? QVariant(m.cost.toDouble()) : QVariant(),
                                        m.actions};
            }
            sheets << sh;
        }

        // Cautions
        {
            Xlsx::SheetData sh{QStringLiteral("Cautions"),
                               {QStringLiteral("Adhérent"), QStringLiteral("Saison"), QStringLiteral("Montant (€)"),
                                QStringLiteral("Moyen"), QStringLiteral("N° chèque"), QStringLiteral("Banque"),
                                QStringLiteral("Reçue le"), QStringLiteral("Statut"), QStringLiteral("Clôturée le"),
                                QStringLiteral("Emprunts en cours"), QStringLiteral("Remarque")},
                               {}};
            for (const Deposit &d : db.deposits())
                sh.rows << QVariantList{d.memberLabel, d.season, d.amount, d.method, d.number, d.bank, dateCell(d.received),
                                        d.status, dateCell(d.closed), double(idx.byMember.value(d.memberId).size()), d.note};
            sheets << sh;
        }

        // Echeances
        {
            Xlsx::SheetData sh{QStringLiteral("Échéances"),
                               {QStringLiteral("Type"), QStringLiteral("Matériel"), QStringLiteral("Opération"),
                                QStringLiteral("Dernière"), QStringLiteral("Échéance"), QStringLiteral("État"),
                                QStringLiteral("Jours restants"), QStringLiteral("Régime")},
                               {}};
            for (const EntityDef *def : Schema::equipment())
                for (const QVariantMap &rec : db.records(*def)) {
                    if (rec.value(QStringLiteral("hors_service")).toInt())
                        continue;
                    const int id = rec.value(QStringLiteral("id")).toInt();
                    for (const Maintenance::Due &d : Maintenance::dues(*def, rec, idx.journal.value(def->kind).value(id))) {
                        if (d.months <= 0)
                            continue;
                        sh.rows << QVariantList{def->label, def->displayLabel(rec), d.op.label, dateCell(d.last), dateCell(d.next),
                                                Maintenance::stateText(d.state),
                                                d.next.isValid() ? QVariant(double(d.daysLeft)) : QVariant(),
                                                def->kind == QLatin1String("blocs") ? Maintenance::blocRegime(rec) : QString()};
                    }
                }
            sheets << sh;
        }
    }

    if (summary)
        for (const Xlsx::SheetData &sh : sheets)
            summary->append(QStringLiteral("%1 : %2 ligne(s)").arg(sh.name).arg(sh.rows.size()));
    return Xlsx::writeWorkbook(path, sheets, error);
}

} // namespace Exporter
