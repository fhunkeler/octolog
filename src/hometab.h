#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
namespace Charts { class StackedBars; class Columns; }
class QProgressBar;
class QTreeWidget;
class QTreeWidgetItem;

// Tableau de bord : chiffres cles, emprunts les plus anciens, entretien des blocs.
class HomeTab : public QWidget
{
    Q_OBJECT
public:
    explicit HomeTab(QWidget *parent = nullptr);
    void reload();

signals:
    // kind : membres, blocs, detendeurs, gilets, emprunts ou entretien
    void openTab(const QString &kind);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Stat {
        QLabel *value;
        QLabel *detail;
        QProgressBar *bar;
    };
    QWidget *makeStatCard(const QString &kind, const QString &title);
    void openItem(QTreeWidgetItem *item);

    QHash<QString, Stat> m_stats;
    QLabel *m_date;
    QTreeWidget *m_oldest;
    QTreeWidget *m_maintenance;
    QLabel *m_maintenanceTitle;
    QLabel *m_oldestMore;
    QLabel *m_maintenanceMore;
    Charts::StackedBars *m_availability;
    Charts::StackedBars *m_maintChart;
    Charts::Columns *m_loansChart;
    static constexpr int MaxListRows = 6;
};
