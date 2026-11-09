#include "searchabletable.h"

#include "searchproxy.h"
#include "ui.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

QStandardItem *makeItem(const QString &text, const QVariant &sortValue)
{
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    item->setData(sortValue.isValid() ? sortValue : QVariant(text), Roles::Sort);
    if (text.contains(QLatin1Char('\n')) || text.size() > 60)
        item->setToolTip(text);
    return item;
}

SearchableTable::SearchableTable(const QString &settingsKey, QWidget *parent)
    : QWidget(parent)
    , m_key(settingsKey)
    , m_model(new QStandardItemModel(this))
    , m_proxy(new SearchProxy(this))
    , m_view(new QTableView(this))
    , m_search(new QLineEdit(this))
    , m_column(new QComboBox(this))
    , m_status(new QComboBox(this))
    , m_count(new QLabel(this))
    , m_barWidget(new QWidget(this))
    , m_bar(new QHBoxLayout(m_barWidget))
{
    m_proxy->setSourceModel(m_model);

    m_search->setPlaceholderText(tr("Rechercher… (ex. : aqualung 15L, taille:M, inspection<2025, -emprunté)"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(Ui::icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
    m_search->setToolTip(tr(
        "<b>Recherche</b><br>"
        "<i>mot1 mot2</i> : toutes les lignes contenant les deux mots<br>"
        "<i>\"texte exact\"</i> : expression entre guillemets<br>"
        "<i>colonne:valeur</i> : la colonne contient la valeur (ex. <i>marque:roth</i>)<br>"
        "<i>colonne=valeur</i> : égalité (ex. <i>taille=m</i>) ; <i>colonne=</i> : vide<br>"
        "<i>colonne&gt;valeur</i>, &lt;, &gt;=, &lt;= : nombres ou dates "
        "(ex. <i>capacité&gt;=15</i>, <i>inspection&lt;2025</i>, <i>retour&gt;01/09/2026</i>)<br>"
        "<i>-mot</i> : exclut les lignes contenant le mot<br>"
        "Le nom de colonne peut être abrégé ; casse et accents sont ignorés."));
    m_column->setToolTip(tr("Colonne dans laquelle chercher les mots libres"));
    m_status->setVisible(false);
    for (QComboBox *c : {m_column, m_status}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(12);
    }
    m_search->setMinimumWidth(180);

    m_bar->setContentsMargins(0, 0, 0, 0);
    m_bar->addWidget(m_search, 1);
    m_bar->addWidget(new QLabel(tr("dans"), this));
    m_bar->addWidget(m_column);
    m_bar->addWidget(m_status);
    m_bar->addWidget(m_count);

    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setAlternatingRowColors(true);
    m_view->setShowGrid(false);
    m_view->setIconSize(QSize(16, 16));
    m_view->setWordWrap(false);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 12);
    m_view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_view->horizontalHeader()->setSectionsMovable(true);
    m_view->horizontalHeader()->setHighlightSections(false);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->horizontalHeader()->setToolTip(tr("Clic droit : choisir les colonnes affichées"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_barWidget);
    layout->addWidget(m_view, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_proxy->setQuery(t);
        updateCount();
    });
    connect(m_column, &QComboBox::currentIndexChanged, this, [this] {
        m_proxy->setSearchColumn(m_column->currentData().toInt());
        updateCount();
    });
    connect(m_status, &QComboBox::currentIndexChanged, this, [this](int i) {
        const RowPredicate pred = m_predicates.value(i);
        if (pred)
            m_proxy->setRowFilter([this, pred](int row) { return pred(m_model->item(row, 0)); });
        else
            m_proxy->setRowFilter({});
        updateCount();
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &SearchableTable::selectionChanged);
    connect(m_view, &QTableView::activated, this, [this](const QModelIndex &idx) {
        emit activated(m_proxy->index(idx.row(), 0).data(Roles::Id).toInt());
    });
    connect(m_view->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &SearchableTable::showHeaderMenu);
    connect(m_view->horizontalHeader(), &QHeaderView::sectionResized, this, [this] {
        if (m_stateRestored)
            saveState();
    });
    connect(m_view->horizontalHeader(), &QHeaderView::sectionMoved, this, &SearchableTable::saveState);
    connect(m_view->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, &SearchableTable::saveState);
}

SearchableTable::~SearchableTable()
{
    saveState();
}

void SearchableTable::saveState() const
{
    if (!m_stateRestored || m_key.isEmpty())
        return;
    QSettings s;
    // Une disposition par jeu de colonnes (les colonnes different entre mode utilisateur et admin)
    s.setValue(QStringLiteral("tables3/%1/%2/header").arg(m_key).arg(m_model->columnCount()),
               m_view->horizontalHeader()->saveState());
}

void SearchableTable::beginReset()
{
    m_savedSelection = selectedIds();
    m_savedCurrent = currentId();
    m_model->removeRows(0, m_model->rowCount());
}

void SearchableTable::setHeaders(const QStringList &headers, const QList<bool> &visibleByDefault)
{
    if (m_model->columnCount() == headers.size()) {
        bool same = true;
        for (int c = 0; c < headers.size() && same; ++c)
            same = m_model->headerData(c, Qt::Horizontal).toString() == headers[c];
        if (same)
            return;
    }
    if (m_model->columnCount() > 0)
        saveState(); // disposition du jeu de colonnes precedent
    m_model->setColumnCount(headers.size());
    m_model->setHorizontalHeaderLabels(headers);
    m_defaultVisible = visibleByDefault;
    updateColumnCombo();

    QSettings s;
    const QByteArray state = s.value(QStringLiteral("tables3/%1/%2/header").arg(m_key).arg(headers.size())).toByteArray();
    m_view->horizontalHeader()->reset();
    if (!m_key.isEmpty() && !state.isEmpty() && m_view->horizontalHeader()->restoreState(state)) {
        m_stateRestored = true;
        return;
    }
    m_stateRestored = false; // largeurs ajustees au contenu en fin de remplissage
    for (int c = 0; c < headers.size(); ++c)
        m_view->setColumnHidden(c, c < visibleByDefault.size() && !visibleByDefault[c]);
    const int sortCol = m_sortHeader.isEmpty() ? 0 : qMax(0, int(headers.indexOf(m_sortHeader)));
    m_view->sortByColumn(sortCol, m_sortOrder);
}

void SearchableTable::setDefaultSort(const QString &header, Qt::SortOrder order)
{
    m_sortHeader = header;
    m_sortOrder = order;
}

void SearchableTable::appendRow(int id, const QList<QStandardItem *> &items)
{
    if (!items.isEmpty())
        items.first()->setData(id, Roles::Id);
    m_model->appendRow(items);
}

void SearchableTable::endReset()
{
    if (!m_stateRestored) {
        m_view->resizeColumnsToContents();
        for (int c = 0; c < m_model->columnCount(); ++c)
            if (m_view->columnWidth(c) > 320)
                m_view->setColumnWidth(c, 320);
        m_stateRestored = true;
    }
    m_proxy->refilter();
    const int scroll = m_view->verticalScrollBar()->value();
    selectIds(m_savedSelection);
    if (m_savedCurrent >= 0 && m_savedSelection.contains(m_savedCurrent))
        for (int r = 0; r < m_proxy->rowCount(); ++r)
            if (m_proxy->index(r, 0).data(Roles::Id).toInt() == m_savedCurrent)
                m_view->selectionModel()->setCurrentIndex(m_proxy->index(r, 0), QItemSelectionModel::NoUpdate);
    m_view->verticalScrollBar()->setValue(scroll);
    updateCount();
    emit selectionChanged();
}

void SearchableTable::addStatusFilter(const QString &label, RowPredicate predicate)
{
    m_predicates << predicate;
    m_status->addItem(label);
    m_status->setVisible(m_status->count() > 1);
}

void SearchableTable::setStatusFilter(int index)
{
    m_status->setCurrentIndex(index);
}

void SearchableTable::setStatusFilterVisible(const QString &label, bool visible)
{
    const int i = m_status->findText(label);
    if (i < 0)
        return;
    if (auto *list = qobject_cast<QListView *>(m_status->view()))
        list->setRowHidden(i, !visible);
    if (!visible && m_status->currentIndex() == i)
        m_status->setCurrentIndex(0);
}

void SearchableTable::setStatusFilter(const QString &label)
{
    const int i = m_status->findText(label);
    if (i >= 0)
        m_status->setCurrentIndex(i);
}

void SearchableTable::setMultiSelection(bool multi)
{
    m_view->setSelectionMode(multi ? QAbstractItemView::ExtendedSelection
                                   : QAbstractItemView::SingleSelection);
}

QList<int> SearchableTable::selectedIds() const
{
    QList<int> ids;
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    for (const QModelIndex &idx : rows)
        ids << m_proxy->index(idx.row(), 0).data(Roles::Id).toInt();
    // Ordre d'affichage
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        int ra = -1, rb = -1;
        for (const QModelIndex &idx : rows) {
            const int id = m_proxy->index(idx.row(), 0).data(Roles::Id).toInt();
            if (id == a)
                ra = idx.row();
            if (id == b)
                rb = idx.row();
        }
        return ra < rb;
    });
    return ids;
}

int SearchableTable::currentId() const
{
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return -1;
    const QModelIndex cur = m_view->currentIndex();
    if (cur.isValid() && m_view->selectionModel()->isRowSelected(cur.row()))
        return m_proxy->index(cur.row(), 0).data(Roles::Id).toInt();
    return m_proxy->index(rows.first().row(), 0).data(Roles::Id).toInt();
}

void SearchableTable::selectIds(const QList<int> &ids)
{
    QItemSelection sel;
    QModelIndex first;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QModelIndex idx = m_proxy->index(r, 0);
        if (ids.contains(idx.data(Roles::Id).toInt())) {
            sel.select(idx, m_proxy->index(r, m_proxy->columnCount() - 1));
            if (!first.isValid())
                first = idx;
        }
    }
    m_view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    if (first.isValid())
        m_view->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
}

bool SearchableTable::selectId(int id)
{
    auto find = [&]() -> QModelIndex {
        for (int r = 0; r < m_proxy->rowCount(); ++r)
            if (m_proxy->index(r, 0).data(Roles::Id).toInt() == id)
                return m_proxy->index(r, 0);
        return {};
    };
    QModelIndex idx = find();
    if (!idx.isValid()) {
        // La fiche est masquee par un filtre : on les retire.
        m_search->clear();
        m_status->setCurrentIndex(0);
        idx = find();
    }
    if (!idx.isValid())
        return false;
    m_view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect
                                                       | QItemSelectionModel::Rows);
    m_view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    return true;
}

int SearchableTable::visibleCount() const
{
    return m_proxy->rowCount();
}

QList<int> SearchableTable::visibleIds() const
{
    QList<int> ids;
    for (int r = 0; r < m_proxy->rowCount(); ++r)
        ids << m_proxy->index(r, 0).data(Roles::Id).toInt();
    return ids;
}

void SearchableTable::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

void SearchableTable::clearSearch()
{
    m_search->clear();
}

void SearchableTable::updateColumnCombo()
{
    const QSignalBlocker block(m_column);
    const QString previous = m_column->currentText();
    m_column->clear();
    m_column->addItem(tr("Toutes les colonnes"), -1);
    for (int c = 0; c < m_model->columnCount(); ++c)
        m_column->addItem(m_model->headerData(c, Qt::Horizontal).toString(), c);
    const int idx = m_column->findText(previous);
    m_column->setCurrentIndex(idx > 0 ? idx : 0);
    m_proxy->setSearchColumn(m_column->currentData().toInt());
}

void SearchableTable::updateCount()
{
    const int total = m_model->rowCount();
    const int shown = m_proxy->rowCount();
    m_count->setText(shown == total ? tr("%n élément(s)", nullptr, total)
                                    : tr("%1 sur %2").arg(shown).arg(total));
}

void SearchableTable::showHeaderMenu(const QPoint &pos)
{
    QMenu menu(this);
    QHeaderView *header = m_view->horizontalHeader();
    for (int c = 0; c < m_model->columnCount(); ++c) {
        QAction *a = menu.addAction(m_model->headerData(c, Qt::Horizontal).toString());
        a->setCheckable(true);
        a->setChecked(!header->isSectionHidden(c));
        connect(a, &QAction::toggled, this, [this, c](bool on) {
            m_view->setColumnHidden(c, !on);
            if (on && m_view->columnWidth(c) < 30)
                m_view->resizeColumnToContents(c);
            saveState();
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Colonnes par défaut"), this, [this] {
        for (int c = 0; c < m_model->columnCount(); ++c) {
            m_view->setColumnHidden(c, c < m_defaultVisible.size() && !m_defaultVisible[c]);
            m_view->horizontalHeader()->moveSection(m_view->horizontalHeader()->visualIndex(c), c);
        }
        m_view->resizeColumnsToContents();
        saveState();
    });
    menu.addAction(tr("Ajuster la largeur des colonnes"), this, [this] {
        m_view->resizeColumnsToContents();
        saveState();
    });
    menu.exec(header->mapToGlobal(pos));
}

QStringList SearchableTable::visibleHeaders() const
{
    QStringList out;
    QHeaderView *h = m_view->horizontalHeader();
    for (int v = 0; v < h->count(); ++v) {
        const int c = h->logicalIndex(v);
        if (!h->isSectionHidden(c))
            out << m_model->headerData(c, Qt::Horizontal).toString();
    }
    return out;
}

QList<QVariantList> SearchableTable::visibleRows() const
{
    QList<QVariantList> out;
    QHeaderView *h = m_view->horizontalHeader();
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        QVariantList row;
        for (int v = 0; v < h->count(); ++v) {
            const int c = h->logicalIndex(v);
            if (h->isSectionHidden(c))
                continue;
            const QModelIndex idx = m_proxy->index(r, c);
            const QVariant typed = idx.data(Roles::Sort);
            const int t = typed.typeId();
            if (t == QMetaType::QDate || t == QMetaType::QDateTime || t == QMetaType::Double
                || t == QMetaType::Int || t == QMetaType::LongLong)
                row << typed;
            else
                row << idx.data(Qt::DisplayRole).toString();
        }
        out << row;
    }
    return out;
}
