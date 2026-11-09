#pragma once

#include <QMainWindow>

class EntityTab;
class LoansTab;
class QLabel;
class QTabWidget;
class SearchableTable;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void showRecord(const QString &kind, int id);
    void importFiles(const QStringList &paths, bool askConfirmation = true);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    SearchableTable *currentTable() const;
    void importDialog();
    void exportView();
    void exportAll();
    void openDatabase();
    void backupDatabase();
    void updateStatus();
    void updateTitle();
    void updateMode();
    void rebuildViewMenu();

    QTabWidget *m_tabs;
    QList<EntityTab *> m_entityTabs;
    LoansTab *m_current;
    class HomeTab *m_home;
    class MaintenanceTab *m_maintenance;
    class DepositsTab *m_deposits;
    QLabel *m_statusLabel;
    QLabel *m_modeLabel = nullptr;
    class QMenu *m_viewMenu = nullptr;
    class QAction *m_adminAction = nullptr;
};
