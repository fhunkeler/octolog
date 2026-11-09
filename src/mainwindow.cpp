#include "mainwindow.h"
#include "admin.h"

#include "charts.h"
#include "database.h"
#include "depositstab.h"
#include "entitytab.h"
#include "exporter.h"
#include "importer.h"
#include "loanstab.h"
#include "maintenancetab.h"
#include "hometab.h"
#include "searchabletable.h"
#include "ui.h"
#include "xlsx.h"

#include <QApplication>
#include <QDebug>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTextStream>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_statusLabel(new QLabel(this))
{
    m_tabs->setDocumentMode(false);
    m_tabs->setIconSize(QSize(20, 20));
    m_home = new HomeTab(this);
    m_tabs->addTab(m_home, Ui::icon(QStringLiteral("home")), tr("Accueil"));
    connect(m_home, &HomeTab::openTab, this, [this](const QString &kind) {
        if (kind == QLatin1String("emprunts")) {
            m_tabs->setCurrentWidget(m_current);
            return;
        }
        if (kind == QLatin1String("entretien")) {
            m_tabs->setCurrentWidget(m_maintenance);
            return;
        }
        for (EntityTab *t : m_entityTabs)
            if (t->def().kind == kind)
                m_tabs->setCurrentWidget(t);
    });
    auto addEntity = [this](const EntityDef &def) {
        auto *tab = new EntityTab(def, this);
        connect(tab, &EntityTab::navigate, this, &MainWindow::showRecord);
        m_entityTabs << tab;
        m_tabs->addTab(tab, Ui::kindIcon(def.kind), def.label);
    };
    addEntity(Schema::members());
    for (const EntityDef *def : Schema::equipment())
        addEntity(*def);

    m_maintenance = new MaintenanceTab(this);
    m_tabs->addTab(m_maintenance, Ui::icon(QStringLiteral("maintenance")), tr("Entretien"));

    m_current = new LoansTab(this);
    m_tabs->addTab(m_current, Ui::icon(QStringLiteral("loans")), tr("Emprunts en cours"));
    m_deposits = new DepositsTab(this);
    m_tabs->addTab(m_deposits, Ui::icon(QStringLiteral("deposit")), tr("Cautions"));

    setCentralWidget(m_tabs);
    statusBar()->addPermanentWidget(m_statusLabel);
    m_modeLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_modeLabel);
    createMenus();

    connect(&Database::instance(), &Database::changed, this, &MainWindow::updateStatus);
    connect(&Admin::instance(), &Admin::changed, this, &MainWindow::updateMode);
    updateStatus();
    updateTitle();
    updateMode();

    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1400, 850);
    // Ouverture sur l'onglet Blocs : l'emprunt de bloc est l'action principale.
    for (EntityTab *t : m_entityTabs)
        if (t->def().kind == QLatin1String("blocs"))
            m_tabs->setCurrentWidget(t);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    event->accept();
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&Fichier"));
    QAction *importAction = file->addAction(Ui::icon(QStringLiteral("import")), tr("&Importer adhérents / matériel / emprunts (Excel)…"),
                                            QKeySequence(tr("Ctrl+I")), this, &MainWindow::importDialog);
    QAction *exportAllAction = file->addAction(Ui::icon(QStringLiteral("export")), tr("Exporter &toutes les données (Excel)…"),
                                               QKeySequence(tr("Ctrl+Shift+E")), this, &MainWindow::exportAll);
    QAction *exportViewAction = file->addAction(Ui::icon(QStringLiteral("export")), tr("&Exporter la vue affichée…"), QKeySequence(tr("Ctrl+E")), this, &MainWindow::exportView);
    exportViewAction->setToolTip(tr("Exporte l'onglet affiché (lignes filtrées, colonnes visibles) en Excel ou CSV"));
    exportAllAction->setToolTip(tr("Extraction complète : un classeur Excel avec une feuille par sujet"));

    // Barre d'outils : import / export toujours a portee de main
    auto *bar = addToolBar(tr("Données"));
    bar->setObjectName(QStringLiteral("dataToolbar"));
    bar->setMovable(false);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    importAction->setIconText(tr("Importer"));
    exportViewAction->setIconText(tr("Exporter la vue"));
    exportAllAction->setIconText(tr("Exporter tout"));
    bar->addAction(importAction);
    bar->addSeparator();
    bar->addAction(exportViewAction);
    bar->addAction(exportAllAction);
    Admin::adminOnly(importAction);

    // Mode admin : bouton a droite de la barre d'outils
    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    m_adminAction = bar->addAction(Ui::icon(QStringLiteral("lock")), tr("Mode admin"));
    m_adminAction->setCheckable(true);
    connect(m_adminAction, &QAction::triggered, this, [this](bool on) {
        if (on)
            Admin::instance().unlock(this);
        else
            Admin::instance().lock();
        m_adminAction->setChecked(Admin::instance().isAdmin());
    });
    connect(m_tabs, &QTabWidget::currentChanged, exportViewAction, [this, exportViewAction] {
        exportViewAction->setEnabled(currentTable() != nullptr);
    });
    exportViewAction->setEnabled(currentTable() != nullptr);
    file->addSeparator();
    Admin::adminOnly(file->addAction(tr("&Ouvrir ou créer une base…"), QKeySequence::Open, this, &MainWindow::openDatabase));
    file->addAction(tr("&Sauvegarder une copie de la base…"), this, &MainWindow::backupDatabase);
    file->addAction(tr("Afficher le dossier de la base"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Database::instance().path()).absolutePath()));
    });
    file->addSeparator();
    file->addAction(tr("&Quitter"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Édition"));
    edit->addAction(Ui::icon(QStringLiteral("search")), tr("&Rechercher"), QKeySequence::Find, this, [this] {
        if (SearchableTable *t = currentTable())
            t->focusSearch();
    });
    edit->addAction(tr("&Effacer la recherche"), QKeySequence(tr("Ctrl+Shift+F")), this, [this] {
        if (SearchableTable *t = currentTable())
            t->clearSearch();
    });

    // Menu Affichage : reconstruit selon les onglets presents (ils changent avec le mode)
    m_viewMenu = menuBar()->addMenu(tr("&Affichage"));

    QMenu *adminMenu = menuBar()->addMenu(tr("A&dministration"));
    adminMenu->addAction(Ui::icon(QStringLiteral("unlock")), tr("Passer en &mode admin…"), this,
                         [this] { Admin::instance().unlock(this); });
    adminMenu->addAction(Ui::icon(QStringLiteral("lock")), tr("Repasser en mode &utilisateur"), QKeySequence(tr("Ctrl+L")), this,
                         [] { Admin::instance().lock(); });
    adminMenu->addSeparator();
    adminMenu->addAction(tr("&Changer le code PIN…"), this, [this] { Admin::instance().changePin(this); });

    QMenu *help = menuBar()->addMenu(tr("&Aide"));
    help->addAction(tr("Aide sur la &recherche"), QKeySequence::HelpContents, this, [this] {
        QMessageBox::information(this, tr("Recherche"), tr(
            "<p>La zone de recherche de chaque onglet filtre la liste au fur et à mesure de la frappe. "
            "Casse et accents sont ignorés, toutes les colonnes (même masquées) sont examinées.</p>"
            "<table cellpadding=3>"
            "<tr><td><b>aqualung 15</b></td><td>lignes contenant les deux mots</td></tr>"
            "<tr><td><b>\"nage avec palmes\"</b></td><td>expression exacte</td></tr>"
            "<tr><td><b>marque:roth</b></td><td>la colonne « Marque… » contient roth</td></tr>"
            "<tr><td><b>taille=m</b></td><td>la colonne Taille vaut exactement M</td></tr>"
            "<tr><td><b>email=</b></td><td>la colonne Email est vide</td></tr>"
            "<tr><td><b>capacité&gt;=15</b></td><td>comparaison numérique</td></tr>"
            "<tr><td><b>inspection&lt;2025</b></td><td>dernière inspection avant 2025</td></tr>"
            "<tr><td><b>emprunt&gt;=01/09/2026</b></td><td>comparaison de dates (jj/mm/aaaa, mm/aaaa ou aaaa)</td></tr>"
            "<tr><td><b>-hors</b></td><td>exclut les lignes contenant « hors »</td></tr>"
            "</table>"
            "<p>Le nom de colonne peut être abrégé (<i>insp</i>, <i>cap</i>…). La liste « dans » limite les mots "
            "libres à une colonne, et la liste de droite filtre par état (disponible, emprunté, droits de prêt…).</p>"
            "<p>Clic droit sur les en-têtes de colonnes pour afficher d'autres caractéristiques.</p>"));
    });
    help->addAction(tr("À &propos d'OctoLog"), this, [this] {
        QMessageBox::about(this, tr("À propos d'OctoLog"),
                           tr("<h3>OctoLog %1</h3><p>Gestion du matériel et des emprunts du club de plongée.</p>"
                              "<p>Base de données : %2</p>")
                               .arg(QApplication::applicationVersion(), Database::instance().path().toHtmlEscaped()));
    });
    help->addAction(tr("À propos de &Qt"), qApp, &QApplication::aboutQt);
}

SearchableTable *MainWindow::currentTable() const
{
    QWidget *w = m_tabs->currentWidget();
    if (auto *e = qobject_cast<EntityTab *>(w))
        return e->table();
    if (auto *l = qobject_cast<LoansTab *>(w))
        return l->table();
    if (auto *m = qobject_cast<MaintenanceTab *>(w))
        return m->currentTable();
    if (auto *d = qobject_cast<DepositsTab *>(w))
        return d->currentTable();
    return nullptr;
}

void MainWindow::showRecord(const QString &kind, int id)
{
    for (EntityTab *tab : m_entityTabs) {
        if (tab->def().kind != kind)
            continue;
        m_tabs->setCurrentWidget(tab);
        if (!tab->showRecord(id))
            statusBar()->showMessage(tr("Fiche introuvable (supprimée ?)"), 5000);
        return;
    }
}

void MainWindow::importDialog()
{
    if (!Admin::require(this))
        return;
    QSettings s;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Importer des fichiers Excel"), s.value(QStringLiteral("import/dir")).toString(),
        tr("Classeurs Excel (*.xlsx);;Tous les fichiers (*)"));
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("import/dir"), QFileInfo(files.first()).absolutePath());
    importFiles(files);
}

void MainWindow::importFiles(const QStringList &paths, bool askConfirmation)
{
    auto confirm = [this](const Importer::Report &r) {
        QApplication::restoreOverrideCursor();
        QMessageBox box(QMessageBox::Question, tr("Import — le fichier fait foi"),
                        tr("Les adhérents et le matériel vont être alignés sur les fichiers importés :\n\n%1")
                            .arg(r.lines.join('\n')),
                        QMessageBox::Apply | QMessageBox::Cancel, this);
        box.setDefaultButton(QMessageBox::Cancel);
        box.button(QMessageBox::Apply)->setText(tr("Appliquer l'import"));
        QStringList details;
        if (!r.removed.isEmpty())
            details << tr("Fiches absentes du fichier :") << r.removed << QString();
        if (!r.warnings.isEmpty())
            details << tr("Remarques :") << r.warnings;
        if (!r.removed.isEmpty())
            box.setInformativeText(tr("<b>%n fiche(s) absente(s) du fichier</b> : les adhérents seront désactivés, "
                                      "le matériel supprimé (l'historique des emprunts est conservé). Voir le détail.",
                                      nullptr, r.removed.size()));
        if (!details.isEmpty())
            box.setDetailedText(details.join('\n'));
        const bool ok = box.exec() == QMessageBox::Apply;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        return ok;
    };

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Importer::Report r = askConfirmation ? Importer::importFiles(paths, confirm) : Importer::importFiles(paths);
    QApplication::restoreOverrideCursor();
    for (const QString &line : r.lines + r.removed)
        qInfo().noquote() << line;
    if (askConfirmation && r.applied)
        statusBar()->showMessage(tr("Import appliqué."), 8000);
    else if (askConfirmation)
        statusBar()->showMessage(tr("Import annulé : aucune modification."), 8000);
}

void MainWindow::exportView()
{
    SearchableTable *t = currentTable();
    if (!t)
        return;
    const QString name = m_tabs->tabText(m_tabs->currentIndex());
    QSettings s;
    QString selectedFilter;
    const QString suggested = QDir(s.value(QStringLiteral("export/dir"), QDir::homePath()).toString())
                                  .filePath(QStringLiteral("%1_%2.xlsx").arg(name, QDate::currentDate().toString("yyyyMMdd")));
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter « %1 »").arg(name), suggested,
                                                tr("Classeur Excel (*.xlsx);;CSV pour Excel (*.csv)"), &selectedFilter);
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("export/dir"), QFileInfo(path).absolutePath());
    const QStringList headers = t->visibleHeaders();
    const QList<QVariantList> rows = t->visibleRows();
    QString err;
    bool ok = false;
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        QFile f(path);
        ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if (ok) {
            QTextStream out(&f);
            out.setGenerateByteOrderMark(true);
            auto cell = [](const QVariant &v) {
                QString s = v.typeId() == QMetaType::QDate ? v.toDate().toString("dd/MM/yyyy")
                          : v.typeId() == QMetaType::QDateTime ? v.toDateTime().toString("dd/MM/yyyy HH:mm")
                                                               : v.toString();
                if (s.contains(';') || s.contains('"') || s.contains('\n'))
                    s = '"' + s.replace('"', QStringLiteral("\"\"")) + '"';
                return s;
            };
            QStringList line;
            for (const QString &h : headers)
                line << cell(h);
            out << line.join(';') << "\r\n";
            for (const QVariantList &r : rows) {
                line.clear();
                for (const QVariant &v : r)
                    line << cell(v);
                out << line.join(';') << "\r\n";
            }
        } else {
            err = f.errorString();
        }
    } else {
        if (!path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive))
            path += QStringLiteral(".xlsx");
        ok = Xlsx::write(path, name, headers, rows, &err);
    }
    if (ok)
        statusBar()->showMessage(tr("%n ligne(s) exportée(s) vers %1", nullptr, rows.size()).arg(path), 8000);
    else
        QMessageBox::warning(this, tr("Export"), tr("L'export a échoué : %1").arg(err));
}

void MainWindow::exportAll()
{
    QSettings s;
    const QString suggested = QDir(s.value(QStringLiteral("export/dir"), QDir::homePath()).toString())
                                  .filePath(QStringLiteral("octolog_extraction_%1.xlsx")
                                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"))));
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter toutes les données"), suggested,
                                                tr("Classeur Excel (*.xlsx)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive))
        path += QStringLiteral(".xlsx");
    s.setValue(QStringLiteral("export/dir"), QFileInfo(path).absolutePath());
    QString err;
    QStringList summary;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = Exporter::exportAll(path, &err, &summary, Admin::instance().isAdmin());
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Export"), tr("L'export a échoué : %1").arg(err));
        return;
    }
    QMessageBox box(QMessageBox::Information, tr("Export terminé"),
                    tr("Toutes les données ont été exportées dans :\n%1\n\n%2").arg(path, summary.join('\n')),
                    QMessageBox::Ok, this);
    QPushButton *open = box.addButton(tr("Ouvrir le fichier"), QMessageBox::ActionRole);
    QPushButton *folder = box.addButton(tr("Ouvrir le dossier"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    else if (box.clickedButton() == folder)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void MainWindow::openDatabase()
{
    if (!Admin::require(this))
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Ouvrir ou créer une base OctoLog"), Database::instance().path(),
        tr("Base OctoLog (*.octolog *.sqlite *.db);;Tous les fichiers (*)"), nullptr,
        QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    QString err;
    if (!Database::instance().open(path, &err)) {
        QMessageBox::critical(this, tr("Base de données"), tr("Impossible d'ouvrir %1 :\n%2").arg(path, err));
        return;
    }
    QSettings().setValue(QStringLiteral("database/path"), path);
    updateTitle();
}

void MainWindow::backupDatabase()
{
    const QString src = Database::instance().path();
    const QString suggested = QFileInfo(src).absoluteDir().filePath(
        QStringLiteral("octolog_sauvegarde_%1.octolog").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm")));
    const QString dst = QFileDialog::getSaveFileName(this, tr("Sauvegarder une copie de la base"), suggested,
                                                     tr("Base OctoLog (*.octolog)"));
    if (dst.isEmpty())
        return;
    if (QFile::exists(dst))
        QFile::remove(dst);
    // VACUUM INTO produit une copie coherente meme base ouverte.
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(dst);
    if (q.exec())
        statusBar()->showMessage(tr("Copie enregistrée : %1").arg(dst), 8000);
    else if (QFile::copy(src, dst))
        statusBar()->showMessage(tr("Copie enregistrée : %1").arg(dst), 8000);
    else
        QMessageBox::warning(this, tr("Sauvegarde"), tr("La copie a échoué."));
}

void MainWindow::updateStatus()
{
    const LoanIndex idx = Database::instance().activeLoanIndex();
    int total = 0;
    QStringList parts;
    for (const EntityDef *def : Schema::equipment()) {
        const int n = idx.byEquipment.value(def->kind).size();
        total += n;
        parts << QStringLiteral("%1 %2").arg(n).arg(def->label.toLower());
    }
    m_statusLabel->setText(tr("%n emprunt(s) en cours", nullptr, total)
                           + (total ? QStringLiteral(" (%1)").arg(parts.join(QStringLiteral(", "))) : QString()));
    m_tabs->setTabText(m_tabs->indexOf(m_current),
                       total ? tr("Emprunts en cours (%1)").arg(total) : tr("Emprunts en cours"));
}

void MainWindow::rebuildViewMenu()
{
    m_viewMenu->clear();
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget *w = m_tabs->widget(i);
        m_viewMenu->addAction(m_tabs->tabIcon(i), m_tabs->tabText(i), QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this,
                              [this, w] { m_tabs->setCurrentWidget(w); });
    }
}

void MainWindow::updateMode()
{
    const bool admin = Admin::instance().isAdmin();
    // Onglets reserves au mode admin : retires (et non simplement masques) en mode utilisateur
    const bool hasMaintenance = m_tabs->indexOf(m_maintenance) >= 0;
    if (admin && !hasMaintenance)
        m_tabs->insertTab(m_tabs->indexOf(m_current), m_maintenance, Ui::icon(QStringLiteral("maintenance")), tr("Entretien"));
    else if (!admin && hasMaintenance)
        m_tabs->removeTab(m_tabs->indexOf(m_maintenance));
    const bool hasDeposits = m_tabs->indexOf(m_deposits) >= 0;
    if (admin && !hasDeposits)
        m_tabs->addTab(m_deposits, Ui::icon(QStringLiteral("deposit")), tr("Cautions"));
    else if (!admin && hasDeposits)
        m_tabs->removeTab(m_tabs->indexOf(m_deposits));
    rebuildViewMenu();
    m_adminAction->setChecked(admin);
    m_adminAction->setIcon(Ui::icon(admin ? QStringLiteral("unlock") : QStringLiteral("lock")));
    m_adminAction->setText(admin ? tr("Mode admin (cliquer pour verrouiller)") : tr("Mode admin"));
    m_adminAction->setToolTip(admin ? tr("Mode admin actif : retour automatique en mode utilisateur après %1 min d'inactivité")
                                          .arg(Admin::IdleMinutes)
                                    : tr("Passer en mode admin (code PIN) pour modifier les fiches, entretiens et cautions"));
    m_modeLabel->setText(admin ? tr("<b style=\"color:%1\">● Mode admin</b>").arg(Charts::borrowed().name())
                               : tr("Mode utilisateur"));
    updateTitle();
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("OctoLog — %1%2")
                       .arg(QFileInfo(Database::instance().path()).fileName(),
                            Admin::instance().isAdmin() ? tr(" — mode admin") : QString()));
}
