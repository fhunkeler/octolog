#pragma once

#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>

// Graphiques legers dessines avec QPainter (aucune dependance supplementaire).
// Regles : traits fins, extremites arrondies, 2 px d'espace entre segments,
// legende toujours presente, valeurs affichees, info-bulle au survol.
namespace Charts {

// Couleurs validees (daltonisme, contraste) pour le theme clair et sombre.
QColor available();
QColor borrowed();
QColor inMaintenance();
QColor outOfService();
QColor statusGood();
QColor statusWarning();
QColor statusCritical();
QColor series();   // serie unique (bleu)

struct Segment {
    QString label;
    QColor color;
};

// Barres horizontales empilees : une ligne par categorie (ex. Blocs), un segment par etat.
class StackedBars : public QWidget
{
    Q_OBJECT
public:
    explicit StackedBars(QWidget *parent = nullptr);
    void setSegments(const QList<Segment> &segments);
    // rows : libelle de ligne + valeur de chaque segment ; key : identifiant renvoye au clic
    void setRows(const QStringList &labels, const QList<QList<int>> &values, const QStringList &keys = {});
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void rowClicked(const QString &key);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    struct Hit {
        QRectF rect;
        int row;
        int segment;
    };
    QList<Hit> layoutHits() const;
    // Legende renvoyee a la ligne : position de chaque entree pour une largeur donnee.
    QList<QPointF> legendPositions(int width, int *height) const;
    int legendHeight() const;

    QList<Segment> m_segments;
    QStringList m_labels;
    QStringList m_keys;
    QList<QList<int>> m_values;
    int m_hoverRow = -1;
    int m_hoverSegment = -1;
};

// Colonnes (serie unique) : ex. emprunts par mois.
class Columns : public QWidget
{
    Q_OBJECT
public:
    explicit Columns(QWidget *parent = nullptr);
    void setData(const QStringList &labels, const QList<int> &values, const QStringList &tooltips);
    void setUnit(const QString &singular, const QString &plural);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF plotArea() const;
    int hitIndex(const QPointF &pos) const;

    QStringList m_labels;
    QStringList m_tooltips;
    QList<int> m_values;
    QString m_singular = QStringLiteral("élément");
    QString m_plural = QStringLiteral("éléments");
    int m_hover = -1;
};

} // namespace Charts
