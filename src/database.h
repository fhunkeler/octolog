#pragma once

#include "deposit.h"
#include "maintenance.h"
#include "schema.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>

class QSqlQuery;

struct Loan {
    int id = 0;
    int memberId = 0;
    QString kind;     // type de materiel (blocs, detendeurs, gilets)
    int equipId = 0;
    QDateTime out;
    QDateTime back;   // invalide tant que le materiel n'est pas rendu
    QString notesOut;
    QString notesBack;
    QString memberLabel;
    QString equipLabel;

    bool active() const { return !back.isValid(); }
    int durationDays() const;
};

// Emprunts et entretiens en cours, indexes pour un acces rapide depuis les vues.
struct LoanIndex {
    QHash<int, QList<Loan>> byMember;
    QHash<QString, QHash<int, Loan>> byEquipment; // kind -> equipId -> emprunt
    QHash<QString, QHash<int, MaintenanceRecord>> inMaintenance; // kind -> equipId -> entretien ouvert
    QHash<QString, QHash<int, QList<MaintenanceRecord>>> journal; // kind -> equipId -> entretiens termines
    QHash<int, Deposit> heldDeposit; // memberId -> caution detenue (la plus recente)
};

class Database : public QObject
{
    Q_OBJECT
public:
    static Database &instance();

    bool open(const QString &path, QString *error);
    QString path() const { return m_path; }

    QList<QVariantMap> records(const EntityDef &def) const;
    QVariantMap record(const EntityDef &def, int id) const;
    QString label(const QString &kind, int id) const;

    int insert(const EntityDef &def, const QVariantMap &values, QString *error);
    bool update(const EntityDef &def, int id, const QVariantMap &values, QString *error);
    bool remove(const EntityDef &def, int id, QString *error);

    // Emprunts. memberId / equipId < 0 : pas de filtre.
    QList<Loan> loans(bool activeOnly, int memberId = -1, const QString &kind = {},
                      int equipId = -1) const;
    LoanIndex activeLoanIndex() const;
    bool borrow(int memberId, const QString &kind, int equipId, const QDateTime &when,
                const QString &notes, QString *error);
    bool giveBack(int loanId, const QDateTime &when, const QString &notes, QString *error);
    bool removeLoan(int loanId, QString *error);
    // Utilise par l'import : insere un emprunt historique s'il n'existe pas deja.
    // Retourne 1 si insere, 0 si deja present, -1 en cas d'erreur.
    int importLoan(const Loan &loan, QString *error);

    // Entretiens. kind vide / equipId < 0 : pas de filtre.
    QList<MaintenanceRecord> maintenances(const QString &kind = {}, int equipId = -1, bool openOnly = false) const;
    // Demarre un entretien (refuse si le materiel est emprunte ou deja en entretien).
    int startMaintenance(const MaintenanceRecord &m, QString *error);
    // Termine un entretien : date de fin, actions, resultat, cout. Met a jour
    // la derniere date d'entretien de la fiche si l'entretien est conforme.
    bool finishMaintenance(int id, const QDate &end, const QString &actions, const QString &result,
                           const QVariant &cost, QString *error);
    bool removeMaintenance(int id, QString *error);

    // Cautions. memberId < 0 : toutes ; heldOnly : non cloturees.
    QList<Deposit> deposits(int memberId = -1, bool heldOnly = false) const;
    int addDeposit(const Deposit &d, QString *error);
    bool updateDeposit(const Deposit &d, QString *error);
    // Cloture (restituee, detruite, encaissee) a une date donnee.
    bool closeDeposit(int id, const QString &status, const QDate &date, const QString &note, QString *error);
    bool removeDeposit(int id, QString *error);

    // Rattache les emprunts dont la fiche a ete supprimee a une fiche de meme
    // libelle (par ex. materiel supprime puis reimporte). Retourne le nombre rattache.
    int relinkOrphanLoans();

    // Parametres stockes dans la base (suivent le fichier).
    QString setting(const QString &key) const;
    void setSetting(const QString &key, const QString &value);

    // Regroupe plusieurs modifications et n'emet changed() qu'une fois.
    void beginBatch();
    void endBatch();

    QSqlDatabase db() const;

signals:
    void changed();
    void opened(); // une (autre) base vient d'etre ouverte

private:
    Database() = default;
    void ensureSchema();
    void notify();
    Loan loanFromQuery(const QSqlQuery &q) const;

    QString m_path;
    int m_batch = 0;
    bool m_pending = false;
};
