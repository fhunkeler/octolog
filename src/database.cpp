#include "database.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

namespace {
const QString kConnection = QStringLiteral("octolog");

QString sqlType(FieldType t)
{
    switch (t) {
    case FieldType::Integer:
    case FieldType::Bool:
        return QStringLiteral("INTEGER");
    case FieldType::Real:
        return QStringLiteral("REAL");
    default:
        return QStringLiteral("TEXT");
    }
}

bool exec(QSqlQuery &q, QString *error)
{
    if (q.exec())
        return true;
    if (error)
        *error = q.lastError().text();
    return false;
}

QString dtToSql(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) : QString();
}
} // namespace

int Loan::durationDays() const
{
    const QDateTime end = back.isValid() ? back : QDateTime::currentDateTime();
    return int(out.date().daysTo(end.date()));
}

Database &Database::instance()
{
    static Database db;
    return db;
}

QSqlDatabase Database::db() const
{
    return QSqlDatabase::database(kConnection);
}

bool Database::open(const QString &path, QString *error)
{
    if (QSqlDatabase::contains(kConnection)) {
        QSqlDatabase::database(kConnection).close();
        QSqlDatabase::removeDatabase(kConnection);
    }
    QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnection);
    d.setDatabaseName(path);
    if (!d.open()) {
        if (error)
            *error = d.lastError().text();
        return false;
    }
    m_path = path;
    QSqlQuery(d).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    ensureSchema();
    emit opened();
    emit changed();
    return true;
}

void Database::ensureSchema()
{
    QSqlDatabase d = db();
    QList<const EntityDef *> defs = {&Schema::members()};
    defs += Schema::equipment();
    for (const EntityDef *def : defs) {
        QSqlQuery q(d);
        q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (id INTEGER PRIMARY KEY AUTOINCREMENT)")
                   .arg(def->table));
        // Ajoute les colonnes manquantes : la base suit l'evolution du schema.
        const QSqlRecord existing = d.record(def->table);
        for (const Field &f : def->fields) {
            if (existing.indexOf(f.key) < 0)
                q.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                           .arg(def->table, f.key, sqlType(f.type)));
        }
    }
    QSqlQuery q(d);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS emprunts ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " membre_id INTEGER NOT NULL,"
        " type_materiel TEXT NOT NULL,"
        " materiel_id INTEGER NOT NULL,"
        " date_emprunt TEXT NOT NULL,"
        " date_retour TEXT,"
        " notes_emprunt TEXT,"
        " notes_retour TEXT,"
        " libelle_membre TEXT,"
        " libelle_materiel TEXT)"));
    // Un materiel ne peut avoir qu'un seul emprunt en cours.
    q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS ux_emprunt_actif ON emprunts"
                          "(type_materiel, materiel_id) WHERE date_retour IS NULL"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS ix_emprunt_membre ON emprunts(membre_id)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS entretiens ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " type_materiel TEXT NOT NULL,"
        " materiel_id INTEGER NOT NULL,"
        " operation TEXT NOT NULL,"
        " responsable TEXT,"
        " date_debut TEXT NOT NULL,"
        " date_fin TEXT,"
        " actions TEXT,"
        " resultat TEXT,"
        " cout REAL,"
        " libelle_materiel TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS parametres (cle TEXT PRIMARY KEY, valeur TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cautions ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " membre_id INTEGER NOT NULL,"
        " saison TEXT,"
        " montant REAL,"
        " moyen TEXT,"
        " numero TEXT,"
        " banque TEXT,"
        " date_reception TEXT NOT NULL,"
        " statut TEXT NOT NULL,"
        " date_cloture TEXT,"
        " remarque TEXT,"
        " libelle_membre TEXT)"));
    // Un materiel ne peut avoir qu'un seul entretien en cours.
    q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS ux_entretien_ouvert ON entretiens"
                          "(type_materiel, materiel_id) WHERE date_fin IS NULL"));
}

void Database::beginBatch()
{
    ++m_batch;
}

void Database::endBatch()
{
    if (--m_batch == 0 && m_pending) {
        m_pending = false;
        emit changed();
    }
}

void Database::notify()
{
    if (m_batch > 0)
        m_pending = true;
    else
        emit changed();
}

QList<QVariantMap> Database::records(const EntityDef &def) const
{
    QList<QVariantMap> out;
    QSqlQuery q(db());
    q.exec(QStringLiteral("SELECT * FROM %1").arg(def.table));
    while (q.next()) {
        QVariantMap m;
        const QSqlRecord r = q.record();
        for (int i = 0; i < r.count(); ++i)
            m.insert(r.fieldName(i), r.value(i));
        out << m;
    }
    return out;
}

QVariantMap Database::record(const EntityDef &def, int id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM %1 WHERE id = ?").arg(def.table));
    q.addBindValue(id);
    QVariantMap m;
    if (q.exec() && q.next()) {
        const QSqlRecord r = q.record();
        for (int i = 0; i < r.count(); ++i)
            m.insert(r.fieldName(i), r.value(i));
    }
    return m;
}

QString Database::label(const QString &kind, int id) const
{
    const EntityDef *def = Schema::byKind(kind);
    if (!def)
        return {};
    const QVariantMap rec = record(*def, id);
    return rec.isEmpty() ? QString() : def->displayLabel(rec);
}

int Database::insert(const EntityDef &def, const QVariantMap &values, QString *error)
{
    QStringList cols, marks;
    QVariantList binds;
    for (const Field &f : def.fields) {
        if (!values.contains(f.key))
            continue;
        cols << f.key;
        marks << QStringLiteral("?");
        binds << values.value(f.key);
    }
    QSqlQuery q(db());
    if (cols.isEmpty())
        q.prepare(QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(def.table));
    else
        q.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                      .arg(def.table, cols.join(','), marks.join(',')));
    for (const QVariant &v : binds)
        q.addBindValue(v);
    if (!exec(q, error))
        return -1;
    notify();
    return q.lastInsertId().toInt();
}

bool Database::update(const EntityDef &def, int id, const QVariantMap &values, QString *error)
{
    QStringList sets;
    QVariantList binds;
    for (const Field &f : def.fields) {
        if (!values.contains(f.key))
            continue;
        sets << f.key + QStringLiteral(" = ?");
        binds << values.value(f.key);
    }
    if (sets.isEmpty())
        return true;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE id = ?").arg(def.table, sets.join(',')));
    for (const QVariant &v : binds)
        q.addBindValue(v);
    q.addBindValue(id);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

bool Database::remove(const EntityDef &def, int id, QString *error)
{
    const bool hasActive = def.isEquipment ? !loans(true, -1, def.kind, id).isEmpty()
                                           : !loans(true, id).isEmpty();
    if (hasActive) {
        if (error)
            *error = QStringLiteral("« %1 » a un emprunt en cours : enregistrez d'abord le retour.")
                         .arg(label(def.kind, id));
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(def.table));
    q.addBindValue(id);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

Loan Database::loanFromQuery(const QSqlQuery &q) const
{
    Loan l;
    l.id = q.value(0).toInt();
    l.memberId = q.value(1).toInt();
    l.kind = q.value(2).toString();
    l.equipId = q.value(3).toInt();
    l.out = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
    if (!q.value(5).isNull() && !q.value(5).toString().isEmpty())
        l.back = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
    l.notesOut = q.value(6).toString();
    l.notesBack = q.value(7).toString();
    l.memberLabel = q.value(8).toString();
    l.equipLabel = q.value(9).toString();
    return l;
}

QList<Loan> Database::loans(bool activeOnly, int memberId, const QString &kind, int equipId) const
{
    QStringList where;
    QVariantList binds;
    if (activeOnly)
        where << QStringLiteral("date_retour IS NULL");
    if (memberId >= 0) {
        where << QStringLiteral("membre_id = ?");
        binds << memberId;
    }
    if (!kind.isEmpty()) {
        where << QStringLiteral("type_materiel = ?");
        binds << kind;
    }
    if (equipId >= 0) {
        where << QStringLiteral("materiel_id = ?");
        binds << equipId;
    }
    QString sql = QStringLiteral(
        "SELECT id, membre_id, type_materiel, materiel_id, date_emprunt, date_retour,"
        " notes_emprunt, notes_retour, libelle_membre, libelle_materiel FROM emprunts");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY date_emprunt DESC, id DESC");
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &v : binds)
        q.addBindValue(v);
    q.exec();

    // Libelles a jour (le libelle memorise sert si la fiche a ete supprimee).
    QHash<QString, QHash<int, QString>> labels;
    auto labelFor = [&](const QString &k, int id, const QString &fallback) {
        if (!labels.contains(k)) {
            QHash<int, QString> &m = labels[k];
            if (const EntityDef *def = Schema::byKind(k))
                for (const QVariantMap &rec : records(*def))
                    m.insert(rec.value("id").toInt(), def->displayLabel(rec));
        }
        return labels[k].value(id, fallback);
    };

    QList<Loan> out;
    while (q.next()) {
        Loan l = loanFromQuery(q);
        l.memberLabel = labelFor(Schema::members().kind, l.memberId, l.memberLabel);
        l.equipLabel = labelFor(l.kind, l.equipId, l.equipLabel);
        out << l;
    }
    return out;
}

LoanIndex Database::activeLoanIndex() const
{
    LoanIndex idx;
    for (const Loan &l : loans(true)) {
        idx.byMember[l.memberId] << l;
        idx.byEquipment[l.kind].insert(l.equipId, l);
    }
    for (const MaintenanceRecord &m : maintenances()) {
        if (m.open())
            idx.inMaintenance[m.kind].insert(m.equipId, m);
        else
            idx.journal[m.kind][m.equipId] << m;
    }
    for (const Deposit &d : deposits(-1, true)) // tri : plus recente d'abord
        if (!idx.heldDeposit.contains(d.memberId))
            idx.heldDeposit.insert(d.memberId, d);
    return idx;
}

bool Database::borrow(int memberId, const QString &kind, int equipId, const QDateTime &when,
                      const QString &notes, QString *error)
{
    const QList<Loan> current = loans(true, -1, kind, equipId);
    if (!current.isEmpty()) {
        if (error)
            *error = QStringLiteral("« %1 » est déjà emprunté par %2.")
                         .arg(current.first().equipLabel, current.first().memberLabel);
        return false;
    }
    const QList<MaintenanceRecord> open = maintenances(kind, equipId, true);
    if (!open.isEmpty()) {
        if (error)
            *error = QStringLiteral("« %1 » est en cours d'entretien (%2, responsable : %3) : "
                                    "il ne peut pas être emprunté avant la fin de l'entretien.")
                         .arg(label(kind, equipId), Maintenance::operationLabel(kind, open.first().operation),
                              open.first().responsible.isEmpty() ? QStringLiteral("non précisé") : open.first().responsible);
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO emprunts (membre_id, type_materiel, materiel_id, date_emprunt,"
        " notes_emprunt, libelle_membre, libelle_materiel) VALUES (?,?,?,?,?,?,?)"));
    q.addBindValue(memberId);
    q.addBindValue(kind);
    q.addBindValue(equipId);
    q.addBindValue(dtToSql(when));
    q.addBindValue(notes.trimmed().isEmpty() ? QVariant() : QVariant(notes.trimmed()));
    q.addBindValue(label(Schema::members().kind, memberId));
    q.addBindValue(label(kind, equipId));
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

bool Database::giveBack(int loanId, const QDateTime &when, const QString &notes, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE emprunts SET date_retour = ?, notes_retour = ?"
                             " WHERE id = ? AND date_retour IS NULL"));
    q.addBindValue(dtToSql(when));
    q.addBindValue(notes.trimmed().isEmpty() ? QVariant() : QVariant(notes.trimmed()));
    q.addBindValue(loanId);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

bool Database::removeLoan(int loanId, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM emprunts WHERE id = ?"));
    q.addBindValue(loanId);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

int Database::importLoan(const Loan &l, QString *error)
{
    QSqlQuery q(db());
    // Un materiel ne peut pas etre emprunte deux fois a la meme minute : cle de doublon.
    q.prepare(QStringLiteral("SELECT id, date_retour FROM emprunts WHERE"
                             " type_materiel = ? AND materiel_id = ? AND date_emprunt = ?"));
    q.addBindValue(l.kind);
    q.addBindValue(l.equipId);
    q.addBindValue(dtToSql(l.out));
    if (q.exec() && q.next()) {
        // Deja connu : on complete seulement un retour survenu depuis.
        if (q.value(1).isNull() && l.back.isValid()) {
            QString err;
            if (!giveBack(q.value(0).toInt(), l.back, l.notesBack, &err)) {
                if (error)
                    *error = err;
                return -1;
            }
        }
        return 0;
    }
    if (l.active() && !loans(true, -1, l.kind, l.equipId).isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 : déjà emprunté par ailleurs").arg(l.equipLabel);
        return -1;
    }
    QSqlQuery ins(db());
    ins.prepare(QStringLiteral(
        "INSERT INTO emprunts (membre_id, type_materiel, materiel_id, date_emprunt, date_retour,"
        " notes_emprunt, notes_retour, libelle_membre, libelle_materiel) VALUES (?,?,?,?,?,?,?,?,?)"));
    ins.addBindValue(l.memberId);
    ins.addBindValue(l.kind);
    ins.addBindValue(l.equipId);
    ins.addBindValue(dtToSql(l.out));
    ins.addBindValue(l.back.isValid() ? QVariant(dtToSql(l.back)) : QVariant());
    ins.addBindValue(l.notesOut.isEmpty() ? QVariant() : QVariant(l.notesOut));
    ins.addBindValue(l.notesBack.isEmpty() ? QVariant() : QVariant(l.notesBack));
    ins.addBindValue(l.memberLabel);
    ins.addBindValue(l.equipLabel);
    if (!exec(ins, error))
        return -1;
    notify();
    return 1;
}

int Database::relinkOrphanLoans()
{
    int relinked = 0;
    QList<const EntityDef *> defs = {&Schema::members()};
    defs += Schema::equipment();
    for (const EntityDef *def : defs) {
        const bool member = def == &Schema::members();
        QHash<QString, int> byLabel;
        for (const QVariantMap &rec : records(*def))
            byLabel.insert(Schema::normalize(def->displayLabel(rec)), rec.value("id").toInt());
        QSqlQuery q(db());
        q.exec(member ? QStringLiteral("SELECT id, libelle_membre FROM emprunts WHERE membre_id NOT IN (SELECT id FROM membres)")
                      : QStringLiteral("SELECT id, libelle_materiel FROM emprunts WHERE type_materiel = '%1'"
                                       " AND materiel_id NOT IN (SELECT id FROM %2)").arg(def->kind, def->table));
        while (q.next()) {
            const int target = byLabel.value(Schema::normalize(q.value(1).toString()), -1);
            if (target < 0)
                continue;
            QSqlQuery u(db());
            u.prepare(member ? QStringLiteral("UPDATE emprunts SET membre_id = ? WHERE id = ?")
                             : QStringLiteral("UPDATE emprunts SET materiel_id = ? WHERE id = ?"));
            u.addBindValue(target);
            u.addBindValue(q.value(0));
            if (u.exec())
                ++relinked;
        }
    }
    if (relinked)
        notify();
    return relinked;
}

QList<MaintenanceRecord> Database::maintenances(const QString &kind, int equipId, bool openOnly) const
{
    QStringList where;
    QVariantList binds;
    if (!kind.isEmpty()) {
        where << QStringLiteral("type_materiel = ?");
        binds << kind;
    }
    if (equipId >= 0) {
        where << QStringLiteral("materiel_id = ?");
        binds << equipId;
    }
    if (openOnly)
        where << QStringLiteral("date_fin IS NULL");
    QString sql = QStringLiteral("SELECT id, type_materiel, materiel_id, operation, responsable, date_debut, date_fin,"
                                 " actions, resultat, cout, libelle_materiel FROM entretiens");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY date_debut DESC, id DESC");
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &v : binds)
        q.addBindValue(v);
    q.exec();

    QHash<QString, QHash<int, QString>> labels;
    QList<MaintenanceRecord> out;
    while (q.next()) {
        MaintenanceRecord m;
        m.id = q.value(0).toInt();
        m.kind = q.value(1).toString();
        m.equipId = q.value(2).toInt();
        m.operation = q.value(3).toString();
        m.responsible = q.value(4).toString();
        m.start = QDate::fromString(q.value(5).toString(), Qt::ISODate);
        m.end = QDate::fromString(q.value(6).toString(), Qt::ISODate);
        m.actions = q.value(7).toString();
        m.result = q.value(8).toString();
        m.cost = q.value(9);
        if (!labels.contains(m.kind)) {
            QHash<int, QString> &h = labels[m.kind];
            if (const EntityDef *def = Schema::byKind(m.kind))
                for (const QVariantMap &rec : records(*def))
                    h.insert(rec.value("id").toInt(), def->displayLabel(rec));
        }
        m.equipLabel = labels[m.kind].value(m.equipId, q.value(10).toString());
        out << m;
    }
    return out;
}

int Database::startMaintenance(const MaintenanceRecord &m, QString *error)
{
    const QString lbl = label(m.kind, m.equipId);
    const QList<Loan> borrowed = loans(true, -1, m.kind, m.equipId);
    if (!borrowed.isEmpty()) {
        if (error)
            *error = QStringLiteral("« %1 » est emprunté par %2 : enregistrez d'abord son retour.")
                         .arg(lbl, borrowed.first().memberLabel);
        return -1;
    }
    if (!maintenances(m.kind, m.equipId, true).isEmpty()) {
        if (error)
            *error = QStringLiteral("« %1 » est déjà en cours d'entretien.").arg(lbl);
        return -1;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO entretiens (type_materiel, materiel_id, operation, responsable, date_debut,"
                             " libelle_materiel) VALUES (?,?,?,?,?,?)"));
    q.addBindValue(m.kind);
    q.addBindValue(m.equipId);
    q.addBindValue(m.operation);
    q.addBindValue(m.responsible.trimmed());
    q.addBindValue(m.start.toString(Qt::ISODate));
    q.addBindValue(lbl);
    if (!exec(q, error))
        return -1;
    notify();
    return q.lastInsertId().toInt();
}

bool Database::finishMaintenance(int id, const QDate &end, const QString &actions, const QString &result,
                                 const QVariant &cost, QString *error)
{
    MaintenanceRecord m;
    bool found = false;
    for (const MaintenanceRecord &x : maintenances({}, -1, true))
        if (x.id == id) {
            m = x;
            found = true;
        }
    if (!found) {
        if (error)
            *error = QStringLiteral("Entretien introuvable ou déjà terminé.");
        return false;
    }
    if (end < m.start) {
        if (error)
            *error = QStringLiteral("%1 : la date de fin précède la date de début.").arg(m.equipLabel);
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE entretiens SET date_fin = ?, actions = ?, resultat = ?, cout = ? WHERE id = ?"));
    q.addBindValue(end.toString(Qt::ISODate));
    q.addBindValue(actions.trimmed().isEmpty() ? QVariant() : QVariant(actions.trimmed()));
    q.addBindValue(result);
    q.addBindValue(cost.isValid() && !cost.toString().isEmpty() ? cost : QVariant());
    q.addBindValue(id);
    if (!exec(q, error))
        return false;

    // Fiche du materiel : derniere date d'entretien (si conforme et plus recente)
    m.end = end;
    m.result = result;
    const EntityDef *def = Schema::byKind(m.kind);
    if (def && m.conform()) {
        const QVariantMap rec = record(*def, m.equipId);
        QVariantMap values;
        auto later = [&](const QString &field) {
            const QDate current = QDate::fromString(rec.value(field).toString(), Qt::ISODate);
            if (def->field(field) && (!current.isValid() || end > current))
                values.insert(field, end.toString(Qt::ISODate));
        };
        for (const Maintenance::Operation &op : Maintenance::operations(m.kind))
            if (op.key == m.operation && !op.dateField.isEmpty())
                later(op.dateField);
        if (m.operation == QLatin1String("requalification"))
            later(QStringLiteral("date_inspection")); // la requalification vaut inspection
        if (m.kind == QLatin1String("blocs") && !values.isEmpty()) {
            if (!m.responsible.isEmpty())
                values.insert(QStringLiteral("inspecteur"), m.responsible);
            values.insert(QStringLiteral("action"), Maintenance::operationLabel(m.kind, m.operation));
        }
        if (m.kind == QLatin1String("gilets") && values.contains(QStringLiteral("date_verif")) && !m.responsible.isEmpty())
            values.insert(QStringLiteral("verifie_par"), m.responsible);
        if (!values.isEmpty())
            update(*def, m.equipId, values, nullptr);
    }
    notify();
    return true;
}

bool Database::removeMaintenance(int id, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM entretiens WHERE id = ?"));
    q.addBindValue(id);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

QList<Deposit> Database::deposits(int memberId, bool heldOnly) const
{
    QStringList where;
    if (memberId >= 0)
        where << QStringLiteral("membre_id = %1").arg(memberId);
    if (heldOnly)
        where << QStringLiteral("date_cloture IS NULL");
    QString sql = QStringLiteral("SELECT id, membre_id, saison, montant, moyen, numero, banque, date_reception, statut,"
                                 " date_cloture, remarque, libelle_membre FROM cautions");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY date_reception DESC, id DESC");
    QSqlQuery q(db());
    q.exec(sql);
    QHash<int, QString> labels;
    for (const QVariantMap &rec : records(Schema::members()))
        labels.insert(rec.value("id").toInt(), Schema::members().displayLabel(rec));
    QList<Deposit> out;
    while (q.next()) {
        Deposit d;
        d.id = q.value(0).toInt();
        d.memberId = q.value(1).toInt();
        d.season = q.value(2).toString();
        d.amount = q.value(3).toDouble();
        d.method = q.value(4).toString();
        d.number = q.value(5).toString();
        d.bank = q.value(6).toString();
        d.received = QDate::fromString(q.value(7).toString(), Qt::ISODate);
        d.status = q.value(8).toString();
        d.closed = QDate::fromString(q.value(9).toString(), Qt::ISODate);
        d.note = q.value(10).toString();
        d.memberLabel = labels.value(d.memberId, q.value(11).toString());
        out << d;
    }
    return out;
}

int Database::addDeposit(const Deposit &d, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO cautions (membre_id, saison, montant, moyen, numero, banque, date_reception,"
                             " statut, remarque, libelle_membre) VALUES (?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(d.memberId);
    q.addBindValue(d.season.isEmpty() ? Deposits::season(d.received) : d.season);
    q.addBindValue(d.amount);
    q.addBindValue(d.method);
    q.addBindValue(d.number.trimmed());
    q.addBindValue(d.bank.trimmed());
    q.addBindValue(d.received.toString(Qt::ISODate));
    q.addBindValue(Deposits::Received);
    q.addBindValue(d.note.trimmed());
    q.addBindValue(label(Schema::members().kind, d.memberId));
    if (!exec(q, error))
        return -1;
    notify();
    return q.lastInsertId().toInt();
}

bool Database::updateDeposit(const Deposit &d, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE cautions SET saison = ?, montant = ?, moyen = ?, numero = ?, banque = ?,"
                             " date_reception = ?, remarque = ? WHERE id = ?"));
    q.addBindValue(d.season);
    q.addBindValue(d.amount);
    q.addBindValue(d.method);
    q.addBindValue(d.number.trimmed());
    q.addBindValue(d.bank.trimmed());
    q.addBindValue(d.received.toString(Qt::ISODate));
    q.addBindValue(d.note.trimmed());
    q.addBindValue(d.id);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

bool Database::closeDeposit(int id, const QString &status, const QDate &date, const QString &note, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE cautions SET statut = ?, date_cloture = ?,"
                             " remarque = CASE WHEN ? = '' THEN remarque ELSE TRIM(COALESCE(remarque, '') || ' ' || ?) END"
                             " WHERE id = ? AND date_cloture IS NULL"));
    q.addBindValue(status);
    q.addBindValue(date.toString(Qt::ISODate));
    q.addBindValue(note.trimmed());
    q.addBindValue(note.trimmed());
    q.addBindValue(id);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

bool Database::removeDeposit(int id, QString *error)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM cautions WHERE id = ?"));
    q.addBindValue(id);
    if (!exec(q, error))
        return false;
    notify();
    return true;
}

QString Database::setting(const QString &key) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT valeur FROM parametres WHERE cle = ?"));
    q.addBindValue(key);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

void Database::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO parametres (cle, valeur) VALUES (?, ?)"
                             " ON CONFLICT(cle) DO UPDATE SET valeur = excluded.valeur"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}
