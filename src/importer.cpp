#include "importer.h"

#include "database.h"
#include "xlsx.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace {

struct Table {
    QString file;
    QStringList headers; // normalises
    QList<QStringList> rows;

    int column(const QString &header) const { return headers.indexOf(Schema::normalize(header)); }
    QString cell(const QStringList &row, int col) const
    {
        return col >= 0 && col < row.size() ? row[col].trimmed() : QString();
    }
};

enum class Kind { Unknown, Members, Equipment, Loans };

struct Detected {
    Kind kind = Kind::Unknown;
    const EntityDef *def = nullptr;
    Table table;
};

bool hasAll(const Table &t, const QStringList &headers)
{
    for (const QString &h : headers)
        if (t.column(h) < 0)
            return false;
    return true;
}

// Cherche dans le classeur la feuille dont les en-tetes correspondent a un type connu.
Detected detect(const QString &path, QString *error)
{
    Detected det;
    const QList<Xlsx::Sheet> sheets = Xlsx::read(path, error);
    for (const Xlsx::Sheet &s : sheets) {
        if (s.rows.isEmpty())
            continue;
        Table t;
        t.file = QFileInfo(path).fileName();
        for (const QString &h : s.rows.first())
            t.headers << Schema::normalize(h);
        t.rows = s.rows.mid(1);

        if (hasAll(t, {"Type matériel", "Identifiant matériel", "Date emprunt"}))
            det.kind = Kind::Loans;
        else if (hasAll(t, {"first_name", "last_name"}) || hasAll(t, {"Nom", "Prénom", "Email"}))
            det = {Kind::Members, &Schema::members(), {}};
        else if (hasAll(t, {"Nom", "Numéro de série"}) && t.column("Capacité(L)") >= 0)
            det = {Kind::Equipment, Schema::byKind("blocs"), {}};
        else if (hasAll(t, {"Identifiant", "Ref 1er"}))
            det = {Kind::Equipment, Schema::byKind("detendeurs"), {}};
        else if (hasAll(t, {"Identifiant", "Taille"}))
            det = {Kind::Equipment, Schema::byKind("gilets"), {}};
        if (det.kind != Kind::Unknown) {
            det.table = t;
            return det;
        }
    }
    return det;
}

QString keyOf(const QStringList &keyFields, const QVariantMap &values)
{
    QStringList parts;
    for (const QString &k : keyFields) {
        const QString v = Schema::normalize(values.value(k).toString());
        if (v.isEmpty())
            return {};
        parts << v;
    }
    return parts.join(QLatin1Char('|'));
}

QString importRecords(const EntityDef &def, const Table &t, QSet<int> *seen, QStringList *warnings)
{
    // colonne Excel pour chaque champ (premier en-tete reconnu non vide par ligne)
    QHash<QString, QList<int>> cols;
    for (const Field &f : def.fields)
        for (const QString &h : f.importHeaders)
            if (int c = t.column(h); c >= 0)
                cols[f.key] << c;

    Database &db = Database::instance();
    QList<QHash<QString, int>> indexes; // une table de rapprochement par cle
    for (const QStringList &key : def.importKeys) {
        QHash<QString, int> idx;
        for (const QVariantMap &rec : db.records(def))
            if (const QString k = keyOf(key, rec); !k.isEmpty())
                idx.insert(k, rec.value("id").toInt());
        indexes << idx;
    }

    int created = 0, updated = 0, skipped = 0;
    for (int r = 0; r < t.rows.size(); ++r) {
        const QStringList &row = t.rows[r];
        // Le fichier fait foi : toute colonne presente est reprise, meme vide.
        QVariantMap values;
        bool anyValue = false;
        for (auto it = cols.cbegin(); it != cols.cend(); ++it) {
            const Field *f = def.field(it.key());
            QVariant v;
            for (int c : it.value()) {
                const QString text = t.cell(row, c);
                if (!text.isEmpty()) {
                    v = Schema::parseValue(*f, text);
                    anyValue = true;
                    break;
                }
            }
            values.insert(f->key, v);
        }
        if (!anyValue)
            continue; // ligne vide
        // Present dans le fichier des adherents : (re)active.
        if (def.field("desactive"))
            values.insert("desactive", 0);
        // Detendeur marque "Vole" dans l'inventaire : hors service.
        if (def.field("lieu") && Schema::normalize(values.value("lieu").toString()).startsWith("vol"))
            values.insert("hors_service", 1);

        int id = -1;
        bool anyKey = false;
        for (int k = 0; k < def.importKeys.size() && id < 0; ++k) {
            const QString key = keyOf(def.importKeys[k], values);
            anyKey |= !key.isEmpty();
            id = key.isEmpty() ? -1 : indexes[k].value(key, -1);
        }
        if (!anyKey) {
            ++skipped;
            continue;
        }
        QString err;
        if (id >= 0) {
            // On ne compte (et n'ecrit) que les fiches reellement modifiees.
            const QVariantMap current = db.record(def, id);
            bool changed = false;
            for (auto it = values.cbegin(); it != values.cend() && !changed; ++it) {
                const Field *f = def.field(it.key());
                changed = Schema::formatValue(*f, current.value(it.key())) != Schema::formatValue(*f, it.value());
            }
            if (changed) {
                if (db.update(def, id, values, &err))
                    ++updated;
                else
                    warnings->append(QStringLiteral("%1 ligne %2 : %3").arg(t.file).arg(r + 2).arg(err));
            }
        } else {
            id = db.insert(def, values, &err);
            if (id < 0) {
                warnings->append(QStringLiteral("%1 ligne %2 : %3").arg(t.file).arg(r + 2).arg(err));
                continue;
            }
            ++created;
        }
        seen->insert(id);
        for (int k = 0; k < def.importKeys.size(); ++k)
            if (const QString key = keyOf(def.importKeys[k], values); !key.isEmpty())
                indexes[k].insert(key, id);
    }
    QString line = QStringLiteral("%1 → %2 : %3 ajouté(s), %4 modifié(s), %5 inchangé(s)")
                       .arg(t.file, def.label).arg(created).arg(updated).arg(seen->size() - created - updated);
    if (skipped)
        line += QStringLiteral(", %1 ligne(s) sans identifiant ignorée(s)").arg(skipped);
    return line;
}

// Le fichier fait foi. Adherents absents : desactives (fiche et historique
// conserves). Materiel absent : supprime, sauf emprunt en cours.
QString removeMissing(const EntityDef &def, const QSet<int> &seen, QStringList *removed, QStringList *warnings)
{
    Database &db = Database::instance();
    const bool members = &def == &Schema::members();
    int count = 0, kept = 0;
    for (const QVariantMap &rec : db.records(def)) {
        const int id = rec.value("id").toInt();
        if (seen.contains(id))
            continue;
        const QString label = def.displayLabel(rec);
        QString err;
        if (members) {
            if (Schema::isInactive(rec))
                continue; // deja desactive
            if (db.update(def, id, {{"desactive", 1}}, &err)) {
                ++count;
                removed->append(QStringLiteral("%1 : %2 (désactivé)").arg(def.label, label));
            }
        } else if (db.remove(def, id, &err)) {
            ++count;
            removed->append(QStringLiteral("%1 : %2 (supprimé)").arg(def.label, label));
        } else {
            ++kept;
            warnings->append(QStringLiteral("%1 « %2 » absent du fichier mais conservé : emprunt en cours")
                                 .arg(def.label, label));
        }
    }
    QString line = members
        ? QStringLiteral("%1 : %2 désactivé(s) car absent(s) du fichier").arg(def.label).arg(count)
        : QStringLiteral("%1 : %2 supprimé(s) car absent(s) du fichier").arg(def.label).arg(count);
    if (kept)
        line += QStringLiteral(", %1 conservé(s) (emprunt en cours)").arg(kept);
    return line;
}

// Code materiel : partie avant le premier " - " ("B018 - 12L - 2S" -> "b018", "13 - Complet" -> "13").
QString equipmentCode(const QString &label)
{
    return Schema::normalize(label.section(QStringLiteral(" - "), 0, 0));
}

QDateTime parseDateTime(const QString &s)
{
    for (const char *fmt : {"dd/MM/yyyy HH:mm:ss", "dd/MM/yyyy HH:mm", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm"}) {
        const QDateTime dt = QDateTime::fromString(s.trimmed(), QString::fromLatin1(fmt));
        if (dt.isValid())
            return dt;
    }
    bool ok = false;
    const double serial = s.toDouble(&ok);
    if (ok && serial > 1)
        return QDateTime(QDate(1899, 12, 30).addDays(qint64(serial)), QTime(0, 0))
            .addMSecs(qint64((serial - qint64(serial)) * 86400000.0 + 0.5));
    const QDate d = Schema::parseDate(s);
    return d.isValid() ? d.startOfDay() : QDateTime();
}

QString importLoans(const Table &t, QStringList *warnings)
{
    Database &db = Database::instance();
    const EntityDef &mdef = Schema::members();

    QHash<QString, int> byEmail, byName;
    for (const QVariantMap &m : db.records(mdef)) {
        const int id = m.value("id").toInt();
        if (const QString e = Schema::normalize(m.value("email").toString()); !e.isEmpty())
            byEmail.insert(e, id);
        byName.insert(Schema::normalize(m.value("nom").toString() + ' ' + m.value("prenom").toString()), id);
    }
    QHash<QString, QHash<QString, int>> byCode; // kind -> code -> id
    for (const EntityDef *def : Schema::equipment())
        for (const QVariantMap &rec : db.records(*def))
            byCode[def->kind].insert(equipmentCode(Schema::formatValue(*def->field(def->codeField),
                                                                      rec.value(def->codeField))),
                                     rec.value("id").toInt());

    const int cNom = t.column("Nom"), cPrenom = t.column("Prénom"), cEmail = t.column("Email");
    const int cType = t.column("Type matériel"), cEquip = t.column("Identifiant matériel");
    const int cOut = t.column("Date emprunt"), cBack = t.column("Date retour");
    const int cNotesOut = t.column("Notes emprunt") >= 0 ? t.column("Notes emprunt") : t.column("Notes");
    const int cNotesBack = t.column("Notes retour");

    int added = 0, known = 0, createdMembers = 0;
    for (int r = 0; r < t.rows.size(); ++r) {
        const QStringList &row = t.rows[r];
        const QString where = QStringLiteral("%1 ligne %2").arg(t.file).arg(r + 2);
        if (t.cell(row, cEquip).isEmpty())
            continue;

        int memberId = byEmail.value(Schema::normalize(t.cell(row, cEmail)), -1);
        if (memberId < 0)
            memberId = byName.value(Schema::normalize(t.cell(row, cNom) + ' ' + t.cell(row, cPrenom)), -1);
        if (memberId < 0 && !t.cell(row, cNom).isEmpty()) {
            // Emprunteur absent du fichier des adherents : on cree sa fiche
            // pour ne pas perdre l'historique.
            QVariantMap m{{"nom", t.cell(row, cNom)},
                          {"prenom", t.cell(row, cPrenom)},
                          {"email", t.cell(row, cEmail)},
                          {"telephone", t.cell(row, t.column("Téléphone"))},
                          {"desactive", 1},
                          {"commentaire", QStringLiteral("Fiche créée automatiquement lors de l'import des emprunts (%1)").arg(t.file)}};
            QString err;
            memberId = db.insert(mdef, m, &err);
            if (memberId >= 0) {
                ++createdMembers;
                if (const QString e = Schema::normalize(m.value("email").toString()); !e.isEmpty())
                    byEmail.insert(e, memberId);
                byName.insert(Schema::normalize(t.cell(row, cNom) + ' ' + t.cell(row, cPrenom)), memberId);
                warnings->append(QStringLiteral("%1 : adhérent %2 %3 absent du fichier des adhérents, fiche créée (désactivée)")
                                     .arg(where, t.cell(row, cNom), t.cell(row, cPrenom)));
            }
        }
        if (memberId < 0) {
            warnings->append(QStringLiteral("%1 : adhérent inconnu %2 %3")
                                 .arg(where, t.cell(row, cNom), t.cell(row, cPrenom)));
            continue;
        }
        const QString typeText = Schema::normalize(t.cell(row, cType));
        const EntityDef *edef = nullptr;
        for (const EntityDef *d : Schema::equipment())
            if (typeText.startsWith(Schema::normalize(d->kind).left(4)))
                edef = d;
        if (!edef) {
            warnings->append(QStringLiteral("%1 : type de matériel inconnu « %2 »").arg(where, t.cell(row, cType)));
            continue;
        }
        const int equipId = byCode[edef->kind].value(equipmentCode(t.cell(row, cEquip)), -1);
        if (equipId < 0) {
            warnings->append(QStringLiteral("%1 : %2 « %3 » introuvable")
                                 .arg(where, edef->singular, t.cell(row, cEquip)));
            continue;
        }
        Loan l;
        l.memberId = memberId;
        l.kind = edef->kind;
        l.equipId = equipId;
        l.out = parseDateTime(t.cell(row, cOut));
        l.back = parseDateTime(t.cell(row, cBack));
        l.notesOut = t.cell(row, cNotesOut);
        l.notesBack = t.cell(row, cNotesBack);
        l.memberLabel = db.label(mdef.kind, memberId);
        l.equipLabel = db.label(edef->kind, equipId);
        if (!l.out.isValid()) {
            warnings->append(QStringLiteral("%1 : date d'emprunt invalide").arg(where));
            continue;
        }
        QString err;
        const int res = db.importLoan(l, &err);
        if (res > 0)
            ++added;
        else if (res == 0)
            ++known;
        else
            warnings->append(QStringLiteral("%1 : %2").arg(where, err));
    }
    QString line = QStringLiteral("%1 → Emprunts : %2 ajouté(s), %3 déjà connu(s)").arg(t.file).arg(added).arg(known);
    if (createdMembers)
        line += QStringLiteral(", %1 fiche(s) adhérent créée(s)").arg(createdMembers);
    return line;
}

} // namespace

namespace Importer {

Report importFiles(const QStringList &paths, const std::function<bool(const Report &)> &confirm)
{
    Report report;
    QList<Detected> items;
    for (const QString &p : paths) {
        QString err;
        Detected d = detect(p, &err);
        if (d.kind == Kind::Unknown) {
            report.lines << QStringLiteral("%1 : format non reconnu %2")
                                .arg(QFileInfo(p).fileName(), err.isEmpty() ? QString() : "(" + err + ")");
            continue;
        }
        items << d;
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Detected &a, const Detected &b) { return int(a.kind) < int(b.kind); });

    Database &db = Database::instance();
    db.beginBatch();
    db.db().transaction();
    QHash<const EntityDef *, QSet<int>> seen;
    QList<const EntityDef *> synced;
    bool removalDone = false;
    for (const Detected &d : items) {
        if (d.kind == Kind::Loans && !removalDone) {
            // Suppressions avant les emprunts, pour que ceux-ci puissent recreer leurs emprunteurs.
            for (const EntityDef *def : synced)
                report.lines << removeMissing(*def, seen[def], &report.removed, &report.warnings);
            removalDone = true;
            if (const int n = db.relinkOrphanLoans())
                report.lines << QStringLiteral("Historique : %1 emprunt(s) rattaché(s) à une fiche recréée").arg(n);
        }
        if (d.kind == Kind::Loans) {
            report.lines << importLoans(d.table, &report.warnings);
        } else {
            if (!synced.contains(d.def))
                synced << d.def;
            report.lines << importRecords(*d.def, d.table, &seen[d.def], &report.warnings);
        }
    }
    if (!removalDone) {
        for (const EntityDef *def : synced)
            report.lines << removeMissing(*def, seen[def], &report.removed, &report.warnings);
        if (const int n = db.relinkOrphanLoans())
            report.lines << QStringLiteral("Historique : %1 emprunt(s) rattaché(s) à une fiche recréée").arg(n);
    }

    report.applied = !confirm || confirm(report);
    if (report.applied)
        db.db().commit();
    else
        db.db().rollback();
    db.endBatch();
    return report;
}

} // namespace Importer
