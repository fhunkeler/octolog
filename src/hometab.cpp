#include "hometab.h"

#include "database.h"
#include "admin.h"
#include "charts.h"
#include "fichedialog.h"
#include "maintenance.h"
#include "tablefill.h"
#include "ui.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum { KindRole = Qt::UserRole + 1, IdRole };

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Disabled, QPalette::WindowText));
    l->setPalette(p);
    return l;
}

QLabel *sectionTitle(const QString &icon, const QString &text, QWidget *parent)
{
    auto *l = new QLabel(QStringLiteral("<img src=\":/icons/%1.svg\" width=\"20\" height=\"20\" style=\"vertical-align:middle\">"
                                        "&nbsp;&nbsp;<b>%2</b>").arg(icon, text.toHtmlEscaped()),
                         parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    l->setFont(f);
    return l;
}

QTreeWidget *makeList(const QStringList &headers, QWidget *parent)
{
    auto *t = new QTreeWidget(parent);
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setAlternatingRowColors(true);
    t->setUniformRowHeights(true);
    t->setFrameShape(QFrame::NoFrame);
    t->header()->setStretchLastSection(true);
    t->setToolTip(QObject::tr("Double-clic : ouvrir la fiche"));
    return t;
}
} // namespace

HomeTab::HomeTab(QWidget *parent)
    : QWidget(parent)
{
    // En-tete
    auto *logo = new QLabel(this);
    logo->setPixmap(QIcon(QStringLiteral(":/octolog.svg")).pixmap(56, 56));
    auto *title = new QLabel(tr("OctoLog"), this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 2);
    tf.setBold(true);
    title->setFont(tf);
    auto *subtitle = mutedLabel(tr("Matériel et emprunts du club Sub Alcatel"), this);
    m_date = mutedLabel(QString(), this);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(title);
    titles->addWidget(subtitle);
    auto *header = new QHBoxLayout;
    header->setSpacing(14);
    header->addWidget(logo);
    header->addLayout(titles);
    header->addStretch();
    header->addWidget(m_date, 0, Qt::AlignBottom);

    // Cartes de chiffres cles
    auto *cards = new QHBoxLayout;
    cards->setSpacing(12);
    cards->addWidget(makeStatCard(QStringLiteral("membres"), tr("Adhérents")));
    for (const EntityDef *def : Schema::equipment())
        cards->addWidget(makeStatCard(def->kind, def->label));
    cards->addWidget(makeStatCard(QStringLiteral("emprunts"), tr("Emprunts en cours")));

    // Graphiques
    auto chartCard = [this](const QString &icon, const QString &title, QWidget *chart, QLabel *note = nullptr) {
        auto *card = Ui::card(this);
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(14, 12, 14, 12);
        l->addWidget(sectionTitle(icon, title, card));
        if (note)
            l->addWidget(note);
        l->addWidget(chart, 1);
        return card;
    };
    m_availability = new Charts::StackedBars(this);
    m_availability->setSegments({{tr("Disponible"), Charts::available()},
                                 {tr("Emprunté"), Charts::borrowed()},
                                 {tr("En entretien"), Charts::inMaintenance()},
                                 {tr("Hors service"), Charts::outOfService()}});
    m_maintChart = new Charts::StackedBars(this);
    m_maintChart->setSegments({{tr("À jour"), Charts::statusGood()},
                               {tr("À prévoir (30 j)"), Charts::statusWarning()},
                               {tr("En retard"), Charts::statusCritical()}});
    m_loansChart = new Charts::Columns(this);
    m_loansChart->setUnit(tr("emprunt"), tr("emprunts"));
    connect(m_availability, &Charts::StackedBars::rowClicked, this, &HomeTab::openTab);
    connect(m_maintChart, &Charts::StackedBars::rowClicked, this, [this] { emit openTab(QStringLiteral("entretien")); });

    auto *charts = new QHBoxLayout;
    charts->setSpacing(12);
    charts->addWidget(chartCard(QStringLiteral("blocs"), tr("Disponibilité du matériel"), m_availability), 1);
    charts->addWidget(chartCard(QStringLiteral("loans"), tr("Emprunts par mois"), m_loansChart), 1);
    QWidget *maintChartCard = chartCard(QStringLiteral("maintenance"), tr("Entretien obligatoire"), m_maintChart);
    charts->addWidget(maintChartCard, 1);
    Admin::adminOnly(maintChartCard);

    // Listes (limitees aux premieres lignes, lien vers le detail)
    auto *oldestCard = Ui::card(this);
    m_oldest = makeList({tr("Matériel"), tr("Adhérent"), tr("Depuis le"), tr("Durée")}, oldestCard);
    auto *oldestLayout = new QVBoxLayout(oldestCard);
    oldestLayout->setContentsMargins(14, 12, 14, 12);
    oldestLayout->addWidget(sectionTitle(QStringLiteral("clock"), tr("Emprunts les plus anciens"), oldestCard));
    oldestLayout->addWidget(m_oldest);
    m_oldestMore = new QLabel(oldestCard);
    connect(m_oldestMore, &QLabel::linkActivated, this, [this] { emit openTab(QStringLiteral("emprunts")); });
    oldestLayout->addWidget(m_oldestMore, 0, Qt::AlignRight);
    oldestLayout->addStretch(1);

    auto *maintCard = Ui::card(this);
    m_maintenance = makeList({tr("Matériel"), tr("À faire"), tr("Échéance")}, maintCard);
    m_maintenanceTitle = sectionTitle(QStringLiteral("warning"), tr("Entretien à prévoir"), maintCard);
    auto *maintLayout = new QVBoxLayout(maintCard);
    maintLayout->setContentsMargins(14, 12, 14, 12);
    maintLayout->addWidget(m_maintenanceTitle);
    maintLayout->addWidget(mutedLabel(tr("En retard ou dans les %1 jours · détail dans l'onglet Entretien")
                                          .arg(Maintenance::SoonDays), maintCard));
    maintLayout->addWidget(m_maintenance);
    m_maintenanceMore = new QLabel(maintCard);
    connect(m_maintenanceMore, &QLabel::linkActivated, this, [this] { emit openTab(QStringLiteral("entretien")); });
    maintLayout->addWidget(m_maintenanceMore, 0, Qt::AlignRight);
    maintLayout->addStretch(1);
    for (QTreeWidget *t : {m_oldest, m_maintenance}) {
        t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        t->setFixedHeight(t->header()->sizeHint().height() + MaxListRows * (fontMetrics().height() + 8) + 4);
    }

    auto *lists = new QHBoxLayout;
    lists->setSpacing(12);
    lists->addWidget(oldestCard, 3);
    lists->addWidget(maintCard, 2);
    Admin::adminOnly(maintCard);

    // Le tout dans une zone defilante (petits ecrans)
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(16);
    layout->addLayout(header);
    layout->addLayout(cards);
    layout->addLayout(charts);
    layout->addLayout(lists);
    layout->addStretch(1);
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_oldest, &QTreeWidget::itemDoubleClicked, this, &HomeTab::openItem);
    connect(m_maintenance, &QTreeWidget::itemDoubleClicked, this, &HomeTab::openItem);
    connect(&Database::instance(), &Database::changed, this, &HomeTab::reload);
    reload();
}

QWidget *HomeTab::makeStatCard(const QString &kind, const QString &title)
{
    auto *card = Ui::card(this);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("kind", kind);
    card->setToolTip(tr("Ouvrir l'onglet %1").arg(title));
    card->installEventFilter(this);

    auto *icon = new QLabel(card);
    icon->setPixmap(Ui::icon(kind == QLatin1String("emprunts") ? QStringLiteral("loans")
                                                               : kind == QLatin1String("membres") ? QStringLiteral("members") : kind)
                        .pixmap(28, 28));
    auto *name = mutedLabel(title, card);
    auto *top = new QHBoxLayout;
    top->addWidget(name);
    top->addStretch();
    top->addWidget(icon);

    Stat s;
    s.value = new QLabel(card);
    QFont vf = s.value->font();
    vf.setPointSizeF(vf.pointSizeF() * 2.4);
    vf.setBold(true);
    s.value->setFont(vf);
    QPalette vp = s.value->palette();
    vp.setColor(QPalette::WindowText, Ui::accent());
    s.value->setPalette(vp);
    s.detail = mutedLabel(QString(), card);
    s.detail->setWordWrap(true);
    s.bar = new QProgressBar(card);
    s.bar->setTextVisible(false);
    s.bar->setMaximumHeight(6);

    auto *l = new QVBoxLayout(card);
    l->setContentsMargins(14, 12, 14, 12);
    l->setSpacing(4);
    l->addLayout(top);
    l->addWidget(s.value);
    l->addWidget(s.detail);
    l->addWidget(s.bar);
    m_stats.insert(kind, s);
    return card;
}

bool HomeTab::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        const QString kind = watched->property("kind").toString();
        if (!kind.isEmpty()) {
            emit openTab(kind);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HomeTab::reload()
{
    Database &db = Database::instance();
    m_date->setText(QLocale().toString(QDate::currentDate(), QStringLiteral("dddd d MMMM yyyy")));
    const LoanIndex index = db.activeLoanIndex();

    // Adherents
    {
        int active = 0, inactive = 0, borrowing = 0;
        for (const QVariantMap &m : db.records(Schema::members())) {
            if (Schema::isInactive(m)) {
                ++inactive;
                continue;
            }
            ++active;
            if (index.byMember.contains(m.value("id").toInt()))
                ++borrowing;
        }
        Stat &s = m_stats[QStringLiteral("membres")];
        s.value->setText(QString::number(active));
        s.detail->setText(tr("actifs · %1 avec du matériel").arg(borrowing)
                          + (inactive ? tr(" · %1 désactivé(s)").arg(inactive) : QString()));
        s.bar->setRange(0, qMax(1, active));
        s.bar->setValue(borrowing);
        s.bar->setToolTip(tr("%1 adhérent(s) actif(s) sur %2 ont du matériel").arg(borrowing).arg(active));
    }

    // Materiel
    int totalLoans = 0;
    for (const EntityDef *def : Schema::equipment()) {
        int total = 0, available = 0, out = 0, inMaint = 0;
        const QHash<int, Loan> loans = index.byEquipment.value(def->kind);
        const auto open = index.inMaintenance.value(def->kind);
        for (const QVariantMap &r : db.records(*def)) {
            ++total;
            const int id = r.value("id").toInt();
            if (loans.contains(id))
                ++out;
            else if (open.contains(id))
                ++inMaint;
            else if (!r.value("hors_service").toInt())
                ++available;
        }
        totalLoans += out;
        Stat &s = m_stats[def->kind];
        s.value->setText(QString::number(available));
        s.detail->setText(tr("disponibles sur %1 · %2 prêté(s)").arg(total).arg(out)
                          + (inMaint ? tr(" · %1 en entretien").arg(inMaint) : QString()));
        s.bar->setRange(0, qMax(1, total));
        s.bar->setValue(available);
        s.bar->setToolTip(tr("%1 disponible(s) sur %2").arg(available).arg(total));
    }

    // Emprunts
    const QList<Loan> active = db.loans(true);
    int longLoans = 0;
    for (const Loan &l : active)
        if (l.durationDays() > 30)
            ++longLoans;
    {
        Stat &s = m_stats[QStringLiteral("emprunts")];
        s.value->setText(QString::number(totalLoans));
        s.detail->setText(tr("dont %1 depuis plus d'un mois").arg(longLoans));
        s.bar->setRange(0, qMax(1, totalLoans));
        s.bar->setValue(totalLoans - longLoans);
        s.bar->setToolTip(tr("%1 emprunt(s) de moins d'un mois").arg(totalLoans - longLoans));
    }

    // Graphique : disponibilite par type
    {
        QStringList labels, keys;
        QList<QList<int>> values;
        for (const EntityDef *def : Schema::equipment()) {
            int avail = 0, out = 0, maint = 0, hs = 0;
            for (const QVariantMap &r : db.records(*def)) {
                const int id = r.value("id").toInt();
                if (index.byEquipment.value(def->kind).contains(id))
                    ++out;
                else if (index.inMaintenance.value(def->kind).contains(id))
                    ++maint;
                else if (r.value("hors_service").toInt())
                    ++hs;
                else
                    ++avail;
            }
            labels << def->label;
            keys << def->kind;
            values << QList<int>{avail, out, maint, hs};
        }
        m_availability->setRows(labels, values, keys);
    }

    // Graphique : emprunts par mois (12 derniers mois)
    {
        const QDate first = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1).addMonths(-11);
        QList<int> counts(12, 0);
        for (const Loan &l : db.loans(false)) {
            const QDate d = l.out.date();
            if (d < first)
                continue;
            const int m = (d.year() - first.year()) * 12 + d.month() - first.month();
            if (m >= 0 && m < 12)
                ++counts[m];
        }
        QStringList labels, tips;
        for (int i = 0; i < 12; ++i) {
            const QDate d = first.addMonths(i);
            labels << QLocale().toString(d, QStringLiteral("MMM")).left(4).remove(QLatin1Char('.'));
            tips << QLocale().toString(d, QStringLiteral("MMMM yyyy"));
        }
        m_loansChart->setData(labels, counts, tips);
    }

    // Graphique : etat de l'entretien obligatoire (blocs, detendeurs)
    {
        QStringList labels, keys;
        QList<QList<int>> values;
        for (const EntityDef *def : Schema::equipment()) {
            int ok = 0, soon = 0, late = 0;
            bool mandatory = false;
            for (const QVariantMap &r : db.records(*def)) {
                if (r.value("hors_service").toInt())
                    continue;
                const QList<Maintenance::Due> dues =
                    Maintenance::dues(*def, r, index.journal.value(def->kind).value(r.value("id").toInt()));
                const Maintenance::State st = Maintenance::worst(dues);
                if (st == Maintenance::State::None)
                    continue;
                mandatory = true;
                if (st == Maintenance::State::Ok)
                    ++ok;
                else if (st == Maintenance::State::Soon)
                    ++soon;
                else
                    ++late;
            }
            if (!mandatory)
                continue;
            labels << def->label;
            keys << def->kind;
            values << QList<int>{ok, soon, late};
        }
        m_maintChart->setRows(labels, values, keys);
    }

    // Emprunts les plus anciens
    m_oldest->clear();
    QList<Loan> sorted = active;
    std::sort(sorted.begin(), sorted.end(), [](const Loan &a, const Loan &b) { return a.out < b.out; });
    for (const Loan &l : sorted.mid(0, MaxListRows)) {
        auto *it = new QTreeWidgetItem({l.equipLabel, l.memberLabel, l.out.toString(QStringLiteral("dd/MM/yyyy")),
                                        TableFill::durationText(l.durationDays())});
        it->setIcon(0, Ui::kindIcon(l.kind));
        it->setData(0, KindRole, l.kind);
        it->setData(0, IdRole, l.equipId);
        if (l.durationDays() > 30)
            it->setForeground(3, Ui::orange());
        m_oldest->addTopLevelItem(it);
    }

    // Entretien a prevoir (tout le materiel, hors materiel hors service)
    m_maintenance->clear();
    QList<QPair<QDate, QTreeWidgetItem *>> todo;
    for (const EntityDef *def : Schema::equipment()) {
        for (const QVariantMap &r : db.records(*def)) {
            if (r.value("hors_service").toInt())
                continue;
            const int id = r.value("id").toInt();
            for (const Maintenance::Due &d : Maintenance::dues(*def, r, index.journal.value(def->kind).value(id))) {
                if (d.state != Maintenance::State::Overdue && d.state != Maintenance::State::Soon)
                    continue;
                auto *it = new QTreeWidgetItem({def->displayLabel(r), d.op.label,
                                                d.next.isValid() ? d.next.toString(QStringLiteral("dd/MM/yyyy")) : tr("date inconnue")});
                it->setIcon(0, Ui::kindIcon(def->kind));
                it->setIcon(1, Ui::dot(Maintenance::stateColor(d.state)));
                it->setToolTip(2, Maintenance::dueText(d));
                if (d.state == Maintenance::State::Overdue)
                    it->setForeground(2, Ui::danger());
                it->setData(0, KindRole, def->kind);
                it->setData(0, IdRole, id);
                todo.append({d.next.isValid() ? d.next : QDate(1900, 1, 1), it});
            }
        }
    }
    // Les plus en retard (ou sans date) en premier
    std::stable_sort(todo.begin(), todo.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (int i = 0; i < todo.size(); ++i) {
        if (i < MaxListRows)
            m_maintenance->addTopLevelItem(todo[i].second);
        else
            delete todo[i].second;
    }
    auto moreLink = [](int total, int shown, const QString &what) {
        return total > shown ? QStringLiteral("<a href=\"#\">%1 ›</a>").arg(what.arg(total))
                             : QString();
    };
    m_oldestMore->setText(moreLink(int(active.size()), MaxListRows, tr("Voir les %1 emprunts en cours")));
    m_maintenanceMore->setText(moreLink(int(todo.size()), MaxListRows, tr("Voir les %1 entretiens à prévoir")));
    m_maintenanceTitle->setText(
        QStringLiteral("<img src=\":/icons/warning.svg\" width=\"20\" height=\"20\" style=\"vertical-align:middle\">"
                       "&nbsp;&nbsp;<b>%1</b>")
            .arg(tr("Entretien à prévoir (%1)").arg(todo.size())));
    for (QTreeWidget *t : {m_oldest, m_maintenance})
        for (int c = 0; c < t->columnCount() - 1; ++c)
            t->resizeColumnToContents(c);
}

void HomeTab::openItem(QTreeWidgetItem *item)
{
    if (item)
        Fiche::open(item->data(0, KindRole).toString(), item->data(0, IdRole).toInt(), this);
}
