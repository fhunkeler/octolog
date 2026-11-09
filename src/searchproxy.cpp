#include "searchproxy.h"

#include "schema.h"

#include <QDate>

namespace {

QStringList tokenize(const QString &s)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    for (QChar c : s) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            cur += c;
        } else if (c.isSpace() && !quoted) {
            if (!cur.isEmpty())
                out << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.isEmpty())
        out << cur;
    return out;
}

QString unquote(QString s)
{
    s.remove(QLatin1Char('"'));
    return s;
}

// Date saisie dans une recherche ; "2025" -> 01/01/2025, "03/2025" -> 01/03/2025.
QDate queryDate(const QString &s, bool upperBound)
{
    bool ok = false;
    const int year = s.toInt(&ok);
    if (ok)
        return year > 1900 && year < 3000 ? (upperBound ? QDate(year, 12, 31) : QDate(year, 1, 1)) : QDate();
    QDate d = QDate::fromString(s, QStringLiteral("M/yyyy"));
    if (d.isValid())
        return upperBound ? d.addMonths(1).addDays(-1) : d;
    return Schema::parseDate(s);
}

} // namespace

SearchProxy::SearchProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(Roles::Sort);
    setSortLocaleAware(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void SearchProxy::setQuery(const QString &query)
{
    m_query = query;
    refilter();
}

void SearchProxy::setSearchColumn(int column)
{
    m_column = column;
    refilter();
}

void SearchProxy::setRowFilter(std::function<bool(int)> filter)
{
    m_rowFilter = std::move(filter);
    refilter();
}

void SearchProxy::refilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    parse();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    parse();
    invalidateRowsFilter();
#endif
}

int SearchProxy::resolveColumn(const QString &name) const
{
    if (!sourceModel() || name.isEmpty())
        return -1;
    const int n = sourceModel()->columnCount();
    QStringList headers;
    for (int c = 0; c < n; ++c)
        headers << Schema::normalize(sourceModel()->headerData(c, Qt::Horizontal).toString());
    for (int c = 0; c < n; ++c)
        if (headers[c] == name)
            return c;
    for (int c = 0; c < n; ++c)
        if (headers[c].startsWith(name))
            return c;
    for (int c = 0; c < n; ++c)
        if (headers[c].contains(name))
            return c;
    // "naissance" doit trouver "Date de naissance" meme en abrege sans espace
    const QString compact = QString(name).remove(QLatin1Char(' '));
    for (int c = 0; c < n; ++c)
        if (QString(headers[c]).remove(QLatin1Char(' ')).contains(compact))
            return c;
    return -1;
}

void SearchProxy::parse()
{
    m_terms.clear();
    for (QString tok : tokenize(m_query)) {
        Term t;
        if (tok.size() > 1 && tok.startsWith(QLatin1Char('-'))) {
            t.negate = true;
            tok.remove(0, 1);
        }
        // Recherche d'un operateur hors guillemets
        int opPos = -1;
        bool quoted = false;
        for (int i = 0; i < tok.size(); ++i) {
            const QChar c = tok[i];
            if (c == QLatin1Char('"'))
                quoted = !quoted;
            else if (!quoted && i > 0 && QStringLiteral(":=<>!").contains(c)) {
                opPos = i;
                break;
            }
        }
        if (opPos > 0) {
            QString op = tok.mid(opPos, 1);
            if (opPos + 1 < tok.size() && tok[opPos + 1] == QLatin1Char('=') && op != QLatin1String("="))
                op += QLatin1Char('=');
            if (op == QLatin1String("!"))
                op = QString(); // "!" seul n'est pas un operateur
            const int col = op.isEmpty() ? -1 : resolveColumn(Schema::normalize(unquote(tok.left(opPos))));
            if (col >= 0) {
                t.column = col;
                t.op = op;
                t.text = Schema::normalize(unquote(tok.mid(opPos + op.size())));
                m_terms << t;
                continue;
            }
        }
        t.column = m_column;
        t.text = Schema::normalize(unquote(tok));
        if (!t.text.isEmpty())
            m_terms << t;
    }
}

bool SearchProxy::matchesCell(const Term &t, int row, int column) const
{
    const QModelIndex idx = sourceModel()->index(row, column);
    const QString cell = Schema::normalize(idx.data(Qt::DisplayRole).toString());

    if (t.op.isEmpty() || t.op == QLatin1String(":"))
        return t.text.isEmpty() ? !cell.isEmpty() : cell.contains(t.text);

    const QVariant typed = idx.data(Roles::Sort);
    int cmp = 0;
    bool comparable = false;
    if (t.text.isEmpty()) {
        comparable = true;
        cmp = cell.isEmpty() ? 0 : 1;
    } else if (typed.typeId() == QMetaType::QDate || typed.typeId() == QMetaType::QDateTime) {
        const QDate cellDate = typed.typeId() == QMetaType::QDate ? typed.toDate() : typed.toDateTime().date();
        const QDate lo = queryDate(t.text, false), hi = queryDate(t.text, true);
        if (lo.isValid() && cellDate.isValid()) {
            comparable = true;
            // "2025" ou "03/2025" designent un intervalle : on compare a ses bornes.
            cmp = cellDate < lo ? -1 : (cellDate > hi ? 1 : 0);
        }
    } else {
        bool okCell = false, okQuery = false;
        const double a = QString(cell).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&okCell);
        const double b = QString(t.text).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&okQuery);
        if (okCell && okQuery) {
            comparable = true;
            cmp = a < b ? -1 : (a > b ? 1 : 0);
        } else if (!cell.isEmpty()) {
            comparable = true;
            cmp = cell.localeAwareCompare(t.text);
            cmp = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
        }
    }
    if (!comparable)
        return false;
    if (t.op == QLatin1String("="))
        return cmp == 0;
    if (t.op == QLatin1String("!="))
        return cmp != 0;
    if (t.op == QLatin1String(">"))
        return cmp > 0;
    if (t.op == QLatin1String("<"))
        return cmp < 0;
    if (t.op == QLatin1String(">="))
        return cmp >= 0;
    if (t.op == QLatin1String("<="))
        return cmp <= 0;
    return false;
}

bool SearchProxy::matches(const Term &t, int row) const
{
    if (t.column >= 0)
        return matchesCell(t, row, t.column);
    const int n = sourceModel()->columnCount();
    for (int c = 0; c < n; ++c)
        if (matchesCell(t, row, c))
            return true;
    return false;
}

bool SearchProxy::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (m_rowFilter && !m_rowFilter(sourceRow))
        return false;
    for (const Term &t : m_terms)
        if (matches(t, sourceRow) == t.negate)
            return false;
    return true;
}

bool SearchProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QVariant a = left.data(Roles::Sort), b = right.data(Roles::Sort);
    // Les cellules vides sont toujours placees en fin de liste croissante.
    const bool emptyA = !a.isValid() || a.toString().isEmpty();
    const bool emptyB = !b.isValid() || b.toString().isEmpty();
    if (emptyA || emptyB)
        return !emptyA && emptyB;
    if (a.typeId() == QMetaType::QString || b.typeId() == QMetaType::QString)
        return QString::localeAwareCompare(Schema::normalize(a.toString()), Schema::normalize(b.toString())) < 0;
    return QVariant::compare(a, b) == QPartialOrdering::Less;
}
