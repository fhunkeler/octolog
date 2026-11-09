#include "admin.h"
#include "database.h"
#include "exporter.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QTextStream>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QDialog>
#include <QDir>
#include <QIcon>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QTranslator>

// Messages de la ligne de commande : sortie standard (et console parente sous Windows,
// ou une application graphique n'a pas de console a elle).
static void cli(const QString &text, bool error = false)
{
#ifdef Q_OS_WIN
    static bool attached = [] {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
            SetConsoleOutputCP(CP_UTF8);
        }
        return true;
    }();
    Q_UNUSED(attached)
#endif
    QTextStream out(error ? stderr : stdout);
    out << text << Qt::endl;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("SubAlcatel"));
    QApplication::setApplicationName(QStringLiteral("OctoLog"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/octolog.svg")));
    QApplication::setDesktopFileName(QStringLiteral("octolog"));
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));

    // Style natif Windows (windows11 / windowsvista selon la version de Windows).
#ifdef Q_OS_WIN
    const QStringList styles = QStyleFactory::keys();
    for (const QString &s : {QStringLiteral("windows11"), QStringLiteral("windowsvista")})
        if (styles.contains(s, Qt::CaseInsensitive)) {
            QApplication::setStyle(s);
            break;
        }
#endif

    // Boutons standard (Oui, Non, Annuler...) en francais.
    QTranslator qtTr;
    if (qtTr.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath))
        || qtTr.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                     QApplication::applicationDirPath() + QStringLiteral("/translations")))
        app.installTranslator(&qtTr);

    QSettings settings;
    QString path = settings.value(QStringLiteral("database/path")).toString();
    QStringList importPaths;
    QString captureDir; // --capture <dossier> : captures d'ecran de chaque onglet puis sortie
    QString exportPath; // --export-all <fichier.xlsx> : extraction complete puis sortie
    bool resetPin = false; // --reset-pin : efface le code PIN admin (redefini au prochain passage)
    QString newPin;        // --set-pin <code> : definit le code PIN admin
    bool startAdmin = false; // --admin : demarre en mode admin sans code PIN
    bool help = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--db") && i + 1 < args.size())
            path = args[++i];
        else if (args[i] == QLatin1String("--import") && i + 1 < args.size())
            importPaths << args[++i];
        else if (args[i] == QLatin1String("--capture") && i + 1 < args.size())
            captureDir = args[++i];
        else if (args[i] == QLatin1String("--export-all") && i + 1 < args.size())
            exportPath = args[++i];
        else if (args[i] == QLatin1String("--reset-pin"))
            resetPin = true;
        else if (args[i] == QLatin1String("--set-pin") && i + 1 < args.size())
            newPin = args[++i];
        else if (args[i] == QLatin1String("--admin"))
            startAdmin = true;
        else if (args[i] == QLatin1String("--help") || args[i] == QLatin1String("-h"))
            help = true;
    }
    if (path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        path = QDir(dir).filePath(QStringLiteral("octolog.octolog"));
    }
    QString err;
    if (!Database::instance().open(path, &err)) {
        QMessageBox::critical(nullptr, QStringLiteral("OctoLog"),
                              QStringLiteral("Impossible d'ouvrir la base %1 :\n%2").arg(path, err));
        return 1;
    }

    if (help) {
        cli(QStringLiteral("OctoLog — options :\n"
                           "  --db <fichier>             base à ouvrir\n"
                           "  --import <fichier|dossier> importer des classeurs Excel au démarrage\n"
                           "  --export-all <fichier>     extraction complète (Excel) puis sortie\n"
                           "  --admin                    démarrer en mode admin sans code PIN\n"
                           "  --reset-pin                effacer le code PIN admin puis sortie\n"
                           "  --set-pin <code>           définir le code PIN admin (4 à 6 chiffres) puis sortie"));
        return 0;
    }
    if (resetPin) {
        Admin::resetPin();
        cli(QStringLiteral("Code PIN admin effacé : il sera redéfini au prochain passage en mode admin."));
        return 0;
    }
    if (!newPin.isEmpty()) {
        if (!Admin::setPin(newPin)) {
            cli(QStringLiteral("Code invalide : 4 à 6 chiffres attendus."), true);
            return 1;
        }
        cli(QStringLiteral("Code PIN admin défini."));
        return 0;
    }
    if (!exportPath.isEmpty()) {
        QStringList summary;
        if (!Exporter::exportAll(exportPath, &err, &summary)) {
            cli(QStringLiteral("Export impossible : %1").arg(err), true);
            return 1;
        }
        cli(QStringLiteral("Extraction écrite dans %1").arg(exportPath));
        for (const QString &line : summary)
            cli(QStringLiteral("  ") + line);
        return 0;
    }

    if (startAdmin)
        Admin::instance().forceAdmin();
    MainWindow w;
    w.show();
    if (!importPaths.isEmpty()) {
        QStringList files;
        for (const QString &p : importPaths) {
            const QFileInfo fi(p);
            if (fi.isDir())
                for (const QFileInfo &f : QDir(p).entryInfoList({QStringLiteral("*.xlsx")}, QDir::Files))
                    files << f.absoluteFilePath();
            else
                files << fi.absoluteFilePath();
        }
        w.importFiles(files, false); // ligne de commande : appliquer sans confirmation
    }
    if (!captureDir.isEmpty()) {
        QTimer::singleShot(300, &w, [&w, captureDir] {
            auto *tabs = w.findChild<QTabWidget *>();
            for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                if (auto *view = tabs->currentWidget()->findChild<QTableView *>())
                    if (view->model()->rowCount() > 2)
                        view->selectRow(2);
                QApplication::processEvents();
                w.grab().save(QDir(captureDir).filePath(QStringLiteral("onglet%1.png").arg(i)));
                if (i == 1 || i == 2) { // Adherents, Blocs
                    // menu contextuel sur la ligne 1
                    auto *view = tabs->currentWidget()->findChild<QTableView *>();
                    view->selectRow(1);
                    const QPoint pos = view->visualRect(view->model()->index(1, 1)).center();
                    QTimer::singleShot(150, [captureDir, i] {
                        if (QWidget *popup = QApplication::activePopupWidget()) {
                            popup->grab().save(QDir(captureDir).filePath(QStringLiteral("menu%1.png").arg(i)));
                            popup->close();
                        }
                    });
                    emit view->customContextMenuRequested(pos);
                }
                if (i == 1 || i == 2)
                    for (auto *b : tabs->currentWidget()->findChildren<QPushButton *>())
                        if (b->text().contains(QStringLiteral("Fiche"))) {
                            b->click();
                            QApplication::processEvents();
                            for (QWidget *d : w.findChildren<QDialog *>())
                                if (d->isVisible()) {
                                    d->grab().save(QDir(captureDir).filePath(QStringLiteral("fiche%1.png").arg(i)));
                                    d->close();
                                }
                        }
            }
            tabs->setCurrentIndex(1);
            QTimer::singleShot(200, [captureDir] {
                QWidget *dlg = QApplication::activeModalWidget();
                if (dlg) {
                    const auto views = dlg->findChildren<QTableView *>();
                    for (int k = 0; k < views.size() && k < 3; ++k) {
                        views[k]->selectRow(0);
                        if (k == 0)
                            views[k]->selectRow(1);
                    }
                    QApplication::processEvents();
                    dlg->grab().save(QDir(captureDir).filePath(QStringLiteral("emprunt.png")));
                    dlg->close();
                }
                QApplication::quit();
            });
            for (auto *b : tabs->currentWidget()->findChildren<QPushButton *>())
                if (b->text().contains(QStringLiteral("Emprunter")))
                    b->click();
        });
    }
    return app.exec();
}
