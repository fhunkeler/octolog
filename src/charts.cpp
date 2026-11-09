#include "charts.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <cmath>

namespace Charts {

namespace {
bool dark()
{
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QColor pick(const char *light, const char *darkColor)
{
    return QColor(QString::fromLatin1(dark() ? darkColor : light));
}

QColor textPrimary(const QWidget *w)
{
    return w->palette().color(QPalette::WindowText);
}

QColor textMuted(const QWidget *w)
{
    QColor c = w->palette().color(QPalette::WindowText);
    c.setAlphaF(0.62);
    return c;
}

QColor gridColor(const QWidget *w)
{
    QColor c = w->palette().color(QPalette::WindowText);
    c.setAlphaF(0.12);
    return c;
}

// Rectangle aux extremites de donnees arrondies (4 px), cote base droit.
QPainterPath roundedEnd(const QRectF &r, bool roundLeft, bool roundRight, bool roundTop = false)
{
    const qreal rad = qMin<qreal>(4.0, qMin(r.width(), r.height()) / 2);
    QPainterPath p;
    if (roundTop) { // colonne : arrondi en haut uniquement
        p.moveTo(r.bottomLeft());
        p.lineTo(r.left(), r.top() + rad);
        p.quadTo(r.topLeft(), QPointF(r.left() + rad, r.top()));
        p.lineTo(r.right() - rad, r.top());
        p.quadTo(r.topRight(), QPointF(r.right(), r.top() + rad));
        p.lineTo(r.bottomRight());
        p.closeSubpath();
        return p;
    }
    const qreal lr = roundLeft ? rad : 0, rr = roundRight ? rad : 0;
    p.moveTo(r.left() + lr, r.top());
    p.lineTo(r.right() - rr, r.top());
    if (rr)
        p.quadTo(r.topRight(), QPointF(r.right(), r.top() + rr));
    p.lineTo(r.right(), r.bottom() - rr);
    if (rr)
        p.quadTo(r.bottomRight(), QPointF(r.right() - rr, r.bottom()));
    p.lineTo(r.left() + lr, r.bottom());
    if (lr)
        p.quadTo(r.bottomLeft(), QPointF(r.left(), r.bottom() - lr));
    p.lineTo(r.left(), r.top() + lr);
    if (lr)
        p.quadTo(r.topLeft(), QPointF(r.left() + lr, r.top()));
    p.closeSubpath();
    return p;
}

constexpr int LabelWidth = 86;
constexpr int RowHeight = 22;
constexpr int RowGap = 14;
constexpr int LegendHeight = 26;
} // namespace

// Palette de reference validee (categorielle : vert d'eau, orange, bleu ; statut : bon, attention, critique)
QColor available() { return pick("#1baf7a", "#199e70"); }
QColor borrowed() { return pick("#eb6834", "#d95926"); }
QColor inMaintenance() { return pick("#2a78d6", "#3987e5"); }
QColor outOfService() { return pick("#a3a29d", "#6b6a64"); }
QColor statusGood() { return QColor(0x0c, 0xa3, 0x0c); }
QColor statusWarning() { return QColor(0xfa, 0xb2, 0x19); }
QColor statusCritical() { return QColor(0xd0, 0x3b, 0x3b); }
QColor series() { return pick("#2a78d6", "#3987e5"); }

// ---------------------------------------------------------------------------

StackedBars::StackedBars(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void StackedBars::setSegments(const QList<Segment> &segments)
{
    m_segments = segments;
    update();
}

void StackedBars::setRows(const QStringList &labels, const QList<QList<int>> &values, const QStringList &keys)
{
    m_labels = labels;
    m_values = values;
    m_keys = keys;
    setCursor(keys.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    updateGeometry();
    update();
}

QList<QPointF> StackedBars::legendPositions(int w, int *height) const
{
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.92);
    const QFontMetrics fm(small);
    QList<QPointF> out;
    qreal x = 0, y = 0;
    for (const Segment &s : m_segments) {
        const qreal itemW = 14 + fm.horizontalAdvance(s.label) + 16;
        if (x > 0 && x + itemW > w) {
            x = 0;
            y += 20;
        }
        out << QPointF(x, y);
        x += itemW;
    }
    if (height)
        *height = int(y) + LegendHeight;
    return out;
}

int StackedBars::legendHeight() const
{
    int h = LegendHeight;
    legendPositions(width(), &h);
    return h;
}

int StackedBars::heightForWidth(int w) const
{
    int lh = LegendHeight;
    legendPositions(w, &lh);
    const int rows = int(m_labels.size());
    return lh + rows * RowHeight + qMax(0, rows - 1) * RowGap + 6;
}

QSize StackedBars::sizeHint() const
{
    return QSize(360, heightForWidth(360));
}

QList<StackedBars::Hit> StackedBars::layoutHits() const
{
    QList<Hit> hits;
    const qreal x0 = LabelWidth;
    const qreal w = qMax<qreal>(10, width() - x0 - 2);
    for (int r = 0; r < m_values.size(); ++r) {
        const QList<int> &v = m_values[r];
        int total = 0;
        for (int x : v)
            total += x;
        const qreal y = legendHeight() + r * (RowHeight + RowGap);
        int nonZero = 0;
        for (int x : v)
            nonZero += x > 0;
        const qreal gaps = 2.0 * qMax(0, nonZero - 1);
        qreal x = x0;
        for (int s = 0; s < v.size(); ++s) {
            if (v[s] <= 0 || total <= 0)
                continue;
            const qreal sw = (w - gaps) * v[s] / total;
            hits.append({QRectF(x, y, sw, RowHeight), r, s});
            x += sw + 2.0; // 2 px d'espace entre segments
        }
    }
    return hits;
}

void StackedBars::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Legende (toujours presente)
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.92);
    p.setFont(small);
    const QList<QPointF> legend = legendPositions(width(), nullptr);
    for (int i = 0; i < m_segments.size(); ++i) {
        const Segment &s = m_segments[i];
        const QPointF at = legend.value(i);
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRoundedRect(QRectF(at.x(), at.y() + 5, 10, 10), 2, 2);
        p.setPen(textMuted(this));
        const int tw = p.fontMetrics().horizontalAdvance(s.label);
        p.drawText(QRectF(at.x() + 14, at.y(), tw + 4, 20), Qt::AlignVCenter | Qt::AlignLeft, s.label);
    }

    // Libelles de lignes
    p.setFont(font());
    for (int r = 0; r < m_labels.size(); ++r) {
        const qreal y = legendHeight() + r * (RowHeight + RowGap);
        p.setPen(textPrimary(this));
        p.drawText(QRectF(0, y, LabelWidth - 10, RowHeight), Qt::AlignVCenter | Qt::AlignLeft, m_labels[r]);
    }

    // Segments + valeurs (texte en couleur de texte, jamais celle de la serie)
    const QList<Hit> hits = layoutHits();
    for (const Hit &h : hits) {
        QColor c = m_segments.value(h.segment).color;
        if (m_hoverRow >= 0 && !(h.row == m_hoverRow && h.segment == m_hoverSegment))
            c.setAlphaF(0.55);
        // premier / dernier segment non nul de la ligne : extremites arrondies
        bool first = true, last = true;
        for (const Hit &o : hits)
            if (o.row == h.row) {
                if (o.segment < h.segment)
                    first = false;
                if (o.segment > h.segment)
                    last = false;
            }
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(roundedEnd(h.rect, first, last));
        const QString v = QString::number(m_values[h.row][h.segment]);
        if (p.fontMetrics().horizontalAdvance(v) + 8 < h.rect.width()) {
            p.setPen(Qt::white);
            QFont b = font();
            b.setBold(true);
            p.setFont(b);
            p.drawText(h.rect, Qt::AlignCenter, v);
            p.setFont(font());
        }
    }
}

void StackedBars::mouseMoveEvent(QMouseEvent *e)
{
    int row = -1, seg = -1;
    for (const Hit &h : layoutHits())
        if (h.rect.adjusted(-1, -4, 1, 4).contains(e->position())) {
            row = h.row;
            seg = h.segment;
        }
    if (row != m_hoverRow || seg != m_hoverSegment) {
        m_hoverRow = row;
        m_hoverSegment = seg;
        update();
    }
    if (row >= 0) {
        int total = 0;
        for (int x : m_values[row])
            total += x;
        const int v = m_values[row][seg];
        QToolTip::showText(e->globalPosition().toPoint(),
                           QStringLiteral("<b>%1</b><br>%2 : %3 (%4 %)")
                               .arg(m_labels[row], m_segments.value(seg).label)
                               .arg(v)
                               .arg(total ? qRound(100.0 * v / total) : 0),
                           this);
    } else {
        QToolTip::hideText();
    }
}

void StackedBars::mouseReleaseEvent(QMouseEvent *e)
{
    for (int r = 0; r < m_labels.size(); ++r) {
        const qreal y = legendHeight() + r * (RowHeight + RowGap);
        if (QRectF(0, y - 4, width(), RowHeight + 8).contains(e->position()) && r < m_keys.size())
            emit rowClicked(m_keys[r]);
    }
}

void StackedBars::leaveEvent(QEvent *)
{
    m_hoverRow = m_hoverSegment = -1;
    update();
}

// ---------------------------------------------------------------------------

Columns::Columns(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(150);
}

void Columns::setData(const QStringList &labels, const QList<int> &values, const QStringList &tooltips)
{
    m_labels = labels;
    m_values = values;
    m_tooltips = tooltips;
    update();
}

void Columns::setUnit(const QString &singular, const QString &plural)
{
    m_singular = singular;
    m_plural = plural;
}

QSize Columns::sizeHint() const
{
    return QSize(420, 170);
}

QRectF Columns::plotArea() const
{
    const int axisW = fontMetrics().horizontalAdvance(QStringLiteral("000")) + 8;
    return QRectF(axisW, 16, width() - axisW - 4, height() - 16 - fontMetrics().height() - 8);
}

int Columns::hitIndex(const QPointF &pos) const
{
    const QRectF area = plotArea();
    if (m_values.isEmpty() || !area.adjusted(0, -16, 0, fontMetrics().height() + 8).contains(pos))
        return -1;
    const qreal slot = area.width() / m_values.size();
    const int i = int((pos.x() - area.left()) / slot);
    return i >= 0 && i < m_values.size() ? i : -1;
}

void Columns::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF area = plotArea();
    int maxV = 1;
    for (int v : m_values)
        maxV = qMax(maxV, v);
    // Echelle "ronde" a 3 graduations
    const double rawStep = maxV / 3.0;
    const double mag = std::pow(10.0, std::floor(std::log10(qMax(1.0, rawStep))));
    double step = std::ceil(rawStep / mag) * mag;
    if (step < 1)
        step = 1;
    const double top = step * 3;

    // Grille recessive + axe
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    p.setFont(small);
    for (int i = 0; i <= 3; ++i) {
        const qreal y = area.bottom() - area.height() * (i * step) / top;
        p.setPen(QPen(gridColor(this), 1));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(textMuted(this));
        p.drawText(QRectF(0, y - 8, area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(int(i * step)));
    }

    if (m_values.isEmpty())
        return;
    const qreal slot = area.width() / m_values.size();
    const qreal barW = qMin<qreal>(28, slot * 0.62);
    int maxIndex = 0;
    for (int i = 1; i < m_values.size(); ++i)
        if (m_values[i] > m_values[maxIndex])
            maxIndex = i;
    for (int i = 0; i < m_values.size(); ++i) {
        const qreal h = area.height() * m_values[i] / top;
        const QRectF bar(area.left() + slot * i + (slot - barW) / 2, area.bottom() - h, barW, h);
        QColor c = series();
        if (m_hover >= 0 && m_hover != i)
            c.setAlphaF(0.55);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        if (h > 0.5)
            p.drawPath(roundedEnd(bar, false, false, true));
        // Etiquettes selectives : maximum, dernier mois, colonne survolee
        if (i == maxIndex || i == m_values.size() - 1 || i == m_hover) {
            p.setPen(textPrimary(this));
            p.drawText(QRectF(bar.center().x() - 20, bar.top() - 16, 40, 14), Qt::AlignCenter, QString::number(m_values[i]));
        }
        // Libelles : un sur deux (en partant du dernier) si la place manque
        int widest = 0;
        for (const QString &l : m_labels)
            widest = qMax(widest, p.fontMetrics().horizontalAdvance(l));
        const int every = widest + 6 > slot ? 2 : 1;
        if ((m_values.size() - 1 - i) % every == 0 || i == m_hover) {
            p.setPen(i == m_hover ? textPrimary(this) : textMuted(this));
            p.drawText(QRectF(area.left() + slot * i - slot / 2, area.bottom() + 4, slot * 2, fontMetrics().height()),
                       Qt::AlignHCenter | Qt::AlignTop, m_labels.value(i));
        }
    }
}

void Columns::mouseMoveEvent(QMouseEvent *e)
{
    const int i = hitIndex(e->position());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
    if (i >= 0)
        QToolTip::showText(e->globalPosition().toPoint(),
                           QStringLiteral("<b>%1</b><br>%2 %3").arg(m_tooltips.value(i)).arg(m_values[i])
                               .arg(m_values[i] > 1 ? m_plural : m_singular),
                           this);
    else
        QToolTip::hideText();
}

void Columns::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

} // namespace Charts
