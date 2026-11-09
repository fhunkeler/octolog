#include "admin.h"
#include "database.h"
#include "exporter.h"
#include "importer.h"
#include "xlsx.h"
#include "searchproxy.h"
#include "searchabletable.h"

#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>

class TstOctoLog : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QStandardItemModel m_model;
    SearchProxy m_proxy;

    int count(const QString &query)
    {
        m_proxy.setQuery(query);
        return m_proxy.rowCount();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QString err;
        QVERIFY2(Database::instance().open(m_dir.filePath("t.octolog"), &err), qPrintable(err));

        m_model.setHorizontalHeaderLabels({"Nom", "Capacité (L)", "Marque bloc", "Dernière inspection (TIV)", "Statut"});
        auto row = [this](const QString &nom, double cap, const QString &marque, const QDate &d, const QString &statut) {
            m_model.appendRow({makeItem(nom), makeItem(QString::number(cap), cap), makeItem(marque),
                               makeItem(d.toString("dd/MM/yyyy"), d), makeItem(statut)});
        };
        row("B001 - 15L - 2S", 15, "Aqualung", QDate(2024, 10, 26), "Disponible");
        row("B002 - 12L - 2S", 12, "Roth", QDate(2025, 3, 1), "Emprunté");
        row("B003 - 7L - 2S", 7, "Scubapro", QDate(2023, 1, 5), "Hors service");
        m_proxy.setSourceModel(&m_model);
    }

    void search_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("expected");
        QTest::newRow("vide") << "" << 3;
        QTest::newRow("mot") << "aqualung" << 1;
        QTest::newRow("casse+accents") << "EMPRUNTE" << 1;
        QTest::newRow("deux mots") << "2s roth" << 1;
        QTest::newRow("champ contient") << "marque:rot" << 1;
        QTest::newRow("champ abrege") << "cap>=12" << 2;
        QTest::newRow("egal") << "cap=7" << 1;
        QTest::newRow("date annee") << "inspection<2025" << 2;
        QTest::newRow("date egal annee") << "insp=2025" << 1;
        QTest::newRow("date precise") << "insp>=26/10/2024" << 2;
        QTest::newRow("negation") << "-hors" << 2;
        QTest::newRow("guillemets") << "\"hors service\"" << 1;
        QTest::newRow("champ vide") << "statut=" << 0;
        QTest::newRow("champ inconnu = texte") << "foo:bar" << 0;
    }
    void search()
    {
        QFETCH(QString, query);
        QFETCH(int, expected);
        QCOMPARE(count(query), expected);
    }

    void borrowAndReturn()
    {
        Database &db = Database::instance();
        QString err;
        const int m1 = db.insert(Schema::members(), {{"nom", "DUPONT"}, {"prenom", "Jean"}, {"pret_bloc", "Oui"}}, &err);
        const int m2 = db.insert(Schema::members(), {{"nom", "MARTIN"}, {"prenom", "Léa"}}, &err);
        const EntityDef &blocs = *Schema::byKind("blocs");
        const int b = db.insert(blocs, {{"nom", "B042 - 15L - 2S"}, {"capacite", 15}}, &err);
        QVERIFY(m1 > 0 && m2 > 0 && b > 0);

        const QDateTime t0(QDate(2026, 9, 1), QTime(10, 0));
        QVERIFY2(db.borrow(m1, "blocs", b, t0, "sortie", &err), qPrintable(err));
        QVERIFY(!db.borrow(m2, "blocs", b, t0, {}, &err)); // deja emprunte
        QCOMPARE(db.activeLoanIndex().byMember.value(m1).size(), 1);
        QCOMPARE(db.activeLoanIndex().byEquipment.value("blocs").value(b).memberId, m1);
        QVERIFY(!db.remove(blocs, b, &err)); // suppression refusee pendant l'emprunt

        const Loan l = db.loans(true, m1).first();
        QCOMPARE(l.equipLabel, QString("B042 - 15L - 2S"));
        QVERIFY(db.giveBack(l.id, t0.addDays(3), "ok", &err));
        QVERIFY(db.loans(true).isEmpty());
        const Loan done = db.loans(false, -1, "blocs", b).first();
        QCOMPARE(done.durationDays(), 3);
        QCOMPARE(done.notesBack, QString("ok"));

        // Pret dans l'autre sens apres retour
        QVERIFY(db.borrow(m2, "blocs", b, t0.addDays(4), {}, &err));
        QCOMPARE(db.loans(false, -1, "blocs", b).size(), 2);
    }

    void severalLoansForOneMember()
    {
        Database &db = Database::instance();
        QString err;
        const int m = db.insert(Schema::members(), {{"nom", "PLONGEUR"}, {"prenom", "Max"}}, &err);
        const int b1 = db.insert(*Schema::byKind("blocs"), {{"nom", "B101 - 12L - 2S"}}, &err);
        const int b2 = db.insert(*Schema::byKind("blocs"), {{"nom", "B102 - 15L - 2S"}}, &err);
        const int d = db.insert(*Schema::byKind("detendeurs"), {{"identifiant", 99}, {"type", "Complet"}}, &err);
        const int g = db.insert(*Schema::byKind("gilets"), {{"identifiant", 77}, {"taille", "M"}}, &err);
        const QDateTime now = QDateTime::currentDateTime();
        QVERIFY(db.borrow(m, "blocs", b1, now, {}, &err));
        QVERIFY(db.borrow(m, "blocs", b2, now, {}, &err));
        QVERIFY(db.borrow(m, "detendeurs", d, now, {}, &err));
        QVERIFY2(db.borrow(m, "gilets", g, now, {}, &err), qPrintable(err));
        QCOMPARE(db.loans(true, m).size(), 4);
        QCOMPARE(db.activeLoanIndex().byMember.value(m).size(), 4);
        // rendre un seul equipement laisse les autres en cours
        QVERIFY(db.giveBack(db.loans(true, m, "blocs", b1).first().id, now, {}, &err));
        QCOMPARE(db.loans(true, m).size(), 3);
    }

    void importIsAuthoritative()
    {
        Database &db = Database::instance();
        const EntityDef &gilets = *Schema::byKind("gilets");
        const QString file = m_dir.filePath("Gilets.xlsx");
        const QStringList headers{"Identifiant", "Marque", "Modèle", "Taille", "Etat"};
        auto countGilets = [&] { return db.records(gilets).size(); };
        auto gilet = [&](int num) {
            for (const QVariantMap &r : db.records(gilets))
                if (r.value("identifiant").toInt() == num)
                    return r;
            return QVariantMap();
        };
        QString err;

        // 1er import : 3 gilets
        QVERIFY(Xlsx::write(file, "Feuil1", headers,
                            {{1, "SCUBAPRO", "T-ONE", "M", "OK"}, {2, "SCUBAPRO", "T-ONE", "S", "OK"},
                             {3, "SCUBAPRO", "GLIDE", "L", "OK"}}, &err));
        const int before = countGilets();
        Importer::importFiles({file});
        QCOMPARE(countGilets(), before + 3);

        // Le gilet 3 est emprunte : il ne pourra pas etre supprime
        const int m = db.insert(Schema::members(), {{"nom", "IMPORT"}, {"prenom", "Test"}}, &err);
        QVERIFY(db.borrow(m, "gilets", gilet(3).value("id").toInt(), QDateTime::currentDateTime(), {}, &err));

        // 2e import : gilet 1 modifie (taille) et etat vide, gilets 2 et 3 absents
        QVERIFY(Xlsx::write(file, "Feuil1", headers, {{1, "SCUBAPRO", "T-ONE", "XL", ""}}, &err));

        // Annulation : rien ne change
        Importer::Report r = Importer::importFiles({file}, [](const Importer::Report &) { return false; });
        QVERIFY(!r.applied);
        QCOMPARE(countGilets(), before + 3);
        QCOMPARE(gilet(1).value("taille").toString(), QString("M"));

        // Application : le fichier fait foi
        r = Importer::importFiles({file}, [](const Importer::Report &) { return true; });
        QVERIFY(r.applied);
        QCOMPARE(gilet(1).value("taille").toString(), QString("XL"));
        QVERIFY(gilet(1).value("etat").isNull());   // case vide -> valeur videe
        QVERIFY(gilet(2).isEmpty());                // absent du fichier -> supprime
        QVERIFY(!gilet(3).isEmpty());               // absent mais emprunte -> conserve
        QCOMPARE(r.removed, QStringList{"Gilets : 2 - S (supprimé)"}); // les gilets empruntes (dont celui d'un autre test) restent
    }

    void importDeactivatesMissingMembers()
    {
        Database &db = Database::instance();
        const EntityDef &mdef = Schema::members();
        const QString file = m_dir.filePath("Adherents.xlsx");
        const QStringList headers{"user_id", "last_name", "first_name", "email", "osm_Pret_Bloc"};
        auto member = [&](const QString &uid) {
            for (const QVariantMap &r : db.records(mdef))
                if (r.value("id_externe").toString() == uid)
                    return r;
            return QVariantMap();
        };
        auto apply = [](const Importer::Report &) { return true; };
        QString err;

        QVERIFY(Xlsx::write(file, "Sheet1", headers,
                            {{"901", "ALPHA", "Anne", "a@x.fr", "Oui"}, {"902", "BETA", "Bruno", "b@x.fr", "Non"}}, &err));
        Importer::importFiles({file}, apply);
        QVERIFY(!Schema::isInactive(member("901")));
        QVERIFY(!Schema::isInactive(member("902")));
        const int historyBefore = db.loans(false).size();

        // BETA absent du nouveau fichier : desactive, pas supprime
        QVERIFY(Xlsx::write(file, "Sheet1", headers, {{"901", "ALPHA", "Anne", "a@x.fr", "Oui"}}, &err));
        const Importer::Report r = Importer::importFiles({file}, apply);
        QVERIFY(!member("902").isEmpty());
        QVERIFY(Schema::isInactive(member("902")));
        QVERIFY(!Schema::isInactive(member("901")));
        QVERIFY(r.removed.contains("Adhérents : BETA Bruno (désactivé)"));
        QCOMPARE(db.loans(false).size(), historyBefore);

        // De retour dans le fichier : reactive
        QVERIFY(Xlsx::write(file, "Sheet1", headers,
                            {{"901", "ALPHA", "Anne", "a@x.fr", "Oui"}, {"902", "BETA", "Bruno", "b@x.fr", "Oui"}}, &err));
        Importer::importFiles({file}, apply);
        QVERIFY(!Schema::isInactive(member("902")));
        QCOMPARE(member("902").value("pret_bloc").toString(), QString("Oui"));
    }

    void maintenanceDues()
    {
        const EntityDef &blocs = *Schema::byKind("blocs");
        const QDate today(2026, 10, 1);
        QVariantMap bloc{{"nom", "B900"}, {"date_inspection", "2025-11-15"}, {"date_requalif", "2021-03-01"}};

        // Regime derogatoire (par defaut) : TIV 12 mois, requalification 6 ans
        QList<Maintenance::Due> d = Maintenance::dues(blocs, bloc, {}, today);
        QCOMPARE(d.size(), 2);
        QCOMPARE(d[0].next, QDate(2026, 11, 15));
        QCOMPARE(d[0].state, Maintenance::State::Ok);     // dans 45 j : au-dela des 30 j
        QCOMPARE(d[1].next, QDate(2027, 3, 1));
        QCOMPARE(d[1].state, Maintenance::State::Ok);

        // Regime standard : requalification tous les 2 ans -> en retard
        bloc.insert("regime", "Standard");
        d = Maintenance::dues(blocs, bloc, {}, today);
        QCOMPARE(d[1].next, QDate(2023, 3, 1));
        QCOMPARE(d[1].state, Maintenance::State::Overdue);

        // Composite : toujours standard
        bloc.insert("regime", "Dérogatoire (TIV)");
        bloc.insert("materiau", "Composite");
        QCOMPARE(Maintenance::blocRegime(bloc), Maintenance::Standard);

        // Le journal (conforme) prime sur une date plus ancienne de la fiche ;
        // une requalification vaut inspection.
        bloc.remove("materiau");
        MaintenanceRecord m;
        m.operation = "requalification";
        m.start = QDate(2026, 9, 1);
        m.end = QDate(2026, 9, 2);
        m.result = Maintenance::Conform;
        d = Maintenance::dues(blocs, bloc, {m}, today);
        QCOMPARE(d[0].last, QDate(2026, 9, 2));
        QCOMPARE(d[1].next, QDate(2032, 9, 2));
        // Non conforme : ne compte pas
        m.result = Maintenance::NonConform;
        d = Maintenance::dues(blocs, bloc, {m}, today);
        QCOMPARE(d[1].last, QDate(2021, 3, 1));

        // Detendeur : revision annuelle ; sans date -> a faire
        const EntityDef &det = *Schema::byKind("detendeurs");
        d = Maintenance::dues(det, {{"identifiant", 1}}, {}, today);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].state, Maintenance::State::Overdue);
        // Gilet : pas d'obligation
        d = Maintenance::dues(*Schema::byKind("gilets"), {{"identifiant", 1}, {"date_verif", "2020-01-01"}}, {}, today);
        QCOMPARE(Maintenance::worst(d), Maintenance::State::None);
    }

    void maintenanceBlocksLoans()
    {
        Database &db = Database::instance();
        const EntityDef &det = *Schema::byKind("detendeurs");
        QString err;
        const int m = db.insert(Schema::members(), {{"nom", "ENTRETIEN"}, {"prenom", "Eva"}}, &err);
        const int d = db.insert(det, {{"identifiant", 501}, {"type", "Complet"}, {"date_revision", "2024-01-01"}}, &err);

        MaintenanceRecord rec;
        rec.kind = "detendeurs";
        rec.equipId = d;
        rec.operation = "revision";
        rec.responsible = "Atelier";
        rec.start = QDate(2026, 9, 20);
        const int mid = db.startMaintenance(rec, &err);
        QVERIFY2(mid > 0, qPrintable(err));
        QVERIFY(db.startMaintenance(rec, &err) < 0);                       // deja en entretien
        QVERIFY(!db.borrow(m, "detendeurs", d, QDateTime::currentDateTime(), {}, &err)); // emprunt refuse
        QVERIFY(err.contains("entretien"));
        QVERIFY(db.activeLoanIndex().inMaintenance.value("detendeurs").contains(d));

        QVERIFY(!db.finishMaintenance(mid, QDate(2026, 9, 1), {}, Maintenance::Conform, {}, &err)); // fin < debut
        QVERIFY(db.finishMaintenance(mid, QDate(2026, 9, 25), "Révision complète", Maintenance::Conform, 52.0, &err));
        QCOMPARE(db.record(det, d).value("date_revision").toString(), QString("2026-09-25"));
        const MaintenanceRecord done = db.maintenances("detendeurs", d).first();
        QCOMPARE(done.cost.toDouble(), 52.0);
        QCOMPARE(done.actions, QString("Révision complète"));
        QVERIFY(!done.open());

        // Emprunt possible apres l'entretien ; entretien refuse pendant l'emprunt
        QVERIFY2(db.borrow(m, "detendeurs", d, QDateTime::currentDateTime(), {}, &err), qPrintable(err));
        QVERIFY(db.startMaintenance(rec, &err) < 0);
        QVERIFY(err.contains("emprunté"));
    }

    void deposits()
    {
        // Pret payant : droit "Oui" ; "Oui (Encadrant)" est gratuit
        QVariantMap m{{"pret_bloc", "Oui (Encadrant)"}, {"pret_detendeur", "Oui"}, {"pret_gilet", "Non"}};
        const QList<const EntityDef *> paid = Deposits::paidTypes(m);
        QCOMPARE(paid.size(), 1);
        QCOMPARE(paid.first()->kind, QString("detendeurs"));
        QVERIFY(!Deposits::needsDeposit({{"pret_bloc", "Oui (Encadrant)"}}));
        QCOMPARE(Deposits::season(QDate(2026, 9, 1)), QString("2026-2027"));
        QCOMPARE(Deposits::season(QDate(2026, 8, 31)), QString("2025-2026"));

        Database &db = Database::instance();
        QString err;
        const int member = db.insert(Schema::members(), {{"nom", "CAUTION"}, {"prenom", "Cléo"}, {"pret_detendeur", "Oui"}}, &err);
        QVERIFY(!db.activeLoanIndex().heldDeposit.contains(member));
        Deposit d;
        d.memberId = member;
        d.amount = 150;
        d.method = "Chèque";
        d.number = "1234567";
        d.bank = "Crédit Mutuel";
        d.received = QDate(2026, 9, 15);
        const int id = db.addDeposit(d, &err);
        QVERIFY2(id > 0, qPrintable(err));
        const Deposit stored = db.deposits(member).first();
        QCOMPARE(stored.season, QString("2026-2027")); // deduite de la date
        QCOMPARE(stored.status, Deposits::Received);
        QVERIFY(stored.held());
        QCOMPARE(db.activeLoanIndex().heldDeposit.value(member).amount, 150.0);

        QVERIFY(db.closeDeposit(id, Deposits::Destroyed, QDate(2027, 7, 1), "fin de saison", &err));
        const Deposit closed = db.deposits(member).first();
        QVERIFY(!closed.held());
        QCOMPARE(closed.status, Deposits::Destroyed);
        QCOMPARE(closed.note, QString("fin de saison"));
        QVERIFY(db.deposits(member, true).isEmpty());
        QVERIFY(!db.activeLoanIndex().heldDeposit.contains(member));
    }

    void exportAllWorkbook()
    {
        const QString file = m_dir.filePath("extraction.xlsx");
        QString err;
        QStringList summary;
        QVERIFY2(Exporter::exportAll(file, &err, &summary), qPrintable(err));
        const QList<Xlsx::Sheet> sheets = Xlsx::read(file, &err);
        QStringList names;
        for (const Xlsx::Sheet &s : sheets)
            names << s.name;
        QCOMPARE(names, (QStringList{"Adhérents", "Blocs", "Détendeurs", "Gilets", "Emprunts en cours",
                                     "Historique des emprunts", "Entretiens", "Cautions", "Échéances"}));
        Database &db = Database::instance();
        // En-tete + une ligne par fiche / emprunt / entretien
        QCOMPARE(sheets[0].rows.size(), db.records(Schema::members()).size() + 1);
        QCOMPARE(sheets[1].rows.size(), db.records(*Schema::byKind("blocs")).size() + 1);
        QCOMPARE(sheets[5].rows.size(), db.loans(false).size() + 1);
        QCOMPARE(sheets[6].rows.size(), db.maintenances().size() + 1);
        QCOMPARE(sheets[0].rows[0].first(), QString("Nom"));
        QCOMPARE(summary.size(), 9);
        QCOMPARE(sheets[7].rows.size(), db.deposits().size() + 1);
        // L'entretien termine du test precedent figure avec son cout et ses actions
        bool found = false;
        for (const QStringList &r : sheets[6].rows)
            found |= r.contains("Révision complète") && r.contains("52");
        QVERIFY(found);
    }

    void adminSettings()
    {
        Database &db = Database::instance();
        db.setSetting("cle_test", "valeur 1");
        QCOMPARE(db.setting("cle_test"), QString("valeur 1"));
        db.setSetting("cle_test", "valeur 2"); // mise a jour
        QCOMPARE(db.setting("cle_test"), QString("valeur 2"));
        QVERIFY(db.setting("inexistante").isEmpty());

        // Mode utilisateur par defaut ; pas de code PIN tant qu'il n'est pas defini
        QVERIFY(!Admin::instance().isAdmin());
        Admin::resetPin();
        QVERIFY(!Admin::instance().hasPin());
        db.setSetting("admin_pin_hash", "abc");
        QVERIFY(Admin::instance().hasPin());
        Admin::resetPin();
        QVERIFY(!Admin::instance().hasPin());
    }

    void userModeExportHasNoAdminData()
    {
        const QString file = m_dir.filePath("extraction_utilisateur.xlsx");
        QString err;
        QVERIFY2(Exporter::exportAll(file, &err, nullptr, false), qPrintable(err));
        const QList<Xlsx::Sheet> sheets = Xlsx::read(file, &err);
        QStringList names;
        for (const Xlsx::Sheet &s : sheets)
            names << s.name;
        QCOMPARE(names, (QStringList{"Adhérents", "Blocs", "Détendeurs", "Gilets", "Emprunts en cours",
                                     "Historique des emprunts"}));
        const QStringList blocHeaders = sheets[1].rows.first();
        QVERIFY(!blocHeaders.contains("État de l'entretien"));
        QVERIFY(!blocHeaders.contains("Prochaine échéance"));
    }

    void pinFromCommandLine()
    {
        QVERIFY(!Admin::setPin("12"));      // trop court
        QVERIFY(!Admin::setPin("12ab"));    // pas des chiffres
        QVERIFY(!Admin::setPin("1234567")); // trop long
        QVERIFY(Admin::setPin("4821"));
        QVERIFY(Admin::instance().hasPin());
        Admin::resetPin();
        QVERIFY(!Admin::instance().hasPin());
    }

    void schemaHelpers()
    {
        QVERIFY(Schema::isYes("Oui (Encadrant)"));
        QVERIFY(!Schema::isYes("Non"));
        QCOMPARE(Schema::parseDate("30/09/2026 23:59:59"), QDate(2026, 9, 30));
        QCOMPARE(Schema::parseDate("45000"), QDate(2023, 3, 15));
        QCOMPARE(Schema::normalize("  Détendeur  Été "), QString("detendeur ete"));
        const EntityDef &gilets = *Schema::byKind("gilets");
        QCOMPARE(gilets.displayLabel({{"identifiant", 34}, {"taille", "XXS"}}), QString("34 - XXS"));
    }
};

QTEST_MAIN(TstOctoLog)
#include "tst_octolog.moc"
