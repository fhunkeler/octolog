#include "ui.h"

#include "charts.h"

#include <QApplication>
#include <QDate>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QVBoxLayout>

namespace Ui {

QIcon icon(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

QIcon kindIcon(const QString &kind)
{
    return icon(kind == QLatin1String("membres") ? QStringLiteral("members") : kind);
}

QColor accent() { return QColor(0x2a, 0x7f, 0xb8); }
QColor orange() { return QColor(0xe8, 0x7d, 0x2c); }
QColor success() { return QColor(0x2e, 0x9d, 0x57); }
QColor danger() { return QColor(0xd9, 0x53, 0x4f); }
QColor neutral() { return QColor(0x8a, 0x93, 0x9b); }

QIcon dot(const QColor &color)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(3, 3, 10, 10));
    return QIcon(pm);
}

QString initials(const QString &label)
{
    const QStringList parts = label.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString out;
    for (const QString &p : parts) {
        if (out.size() == 2)
            break;
        if (p.at(0).isLetterOrNumber())
            out += p.at(0).toUpper();
    }
    return out;
}

QPixmap avatar(const QString &text, const QColor &color, int size)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(0, 0, size, size));
    QFont f = qApp->font();
    f.setBold(true);
    f.setPixelSize(size * 0.38);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, text);
    return pm;
}

void setPill(QLabel *label, const QString &text, const QColor &color)
{
    label->setText(text);
    label->setVisible(!text.isEmpty());
    label->setStyleSheet(QStringLiteral("QLabel { background: %1; color: white; border-radius: 9px;"
                                        " padding: 2px 10px; font-weight: 600; }")
                             .arg(color.name()));
}

QFrame *card(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setObjectName(QStringLiteral("card"));
    f->setStyleSheet(QStringLiteral("QFrame#card { background: palette(base); border: 1px solid palette(mid);"
                                    " border-radius: 10px; }"));
    return f;
}

QWidget *separator(QWidget *parent)
{
    // Ligne de 1 px encadree d'espace : bien visible sans alourdir.
    auto *box = new QWidget(parent);
    auto *l = new QVBoxLayout(box);
    l->setContentsMargins(0, 8, 0, 8);
    auto *line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: palette(mid); border: none;"));
    l->addWidget(line);
    return box;
}

QString subtitle(const EntityDef &def, const QVariantMap &rec)
{
    auto v = [&](const QString &key) {
        const Field *f = def.field(key);
        return f ? Schema::formatValue(*f, rec.value(key)) : QString();
    };
    QStringList parts;
    auto add = [&](const QString &text, const QString &value) {
        if (!value.isEmpty())
            parts << text.arg(value);
    };
    if (def.kind == QLatin1String("membres")) {
        add(QStringLiteral("%1"), v("niveau_prepare") == QLatin1String("Aucun") ? QString() : v("niveau_prepare"));
        add(QStringLiteral("%1"), v("telephone"));
        add(QStringLiteral("%1"), v("email"));
    } else if (def.kind == QLatin1String("blocs")) {
        add(QStringLiteral("%1 L"), v("capacite"));
        add(QStringLiteral("%1"), v("gaz"));
        add(QStringLiteral("TIV %1"), v("date_inspection"));
        add(QStringLiteral("requalif. %1"), v("date_requalif"));
    } else if (def.kind == QLatin1String("detendeurs")) {
        add(QStringLiteral("%1"), QStringLiteral("%1 %2").arg(v("marque"), v("modele")).trimmed());
        add(QStringLiteral("%1"), v("type"));
        add(QStringLiteral("révision %1"), v("date_revision"));
    } else {
        add(QStringLiteral("Taille %1"), v("taille"));
        add(QStringLiteral("%1"), QStringLiteral("%1 %2").arg(v("marque"), v("modele")).trimmed());
        add(QStringLiteral("vérifié le %1"), v("date_verif"));
    }
    return parts.join(QStringLiteral("  ·  "));
}

Header::Header(QWidget *parent)
    : QFrame(parent)
    , m_avatar(new QLabel(this))
    , m_title(new QLabel(this))
    , m_subtitle(new QLabel(this))
    , m_pill(new QLabel(this))
    , m_details(new QLabel(this))
    , m_buttons(new QHBoxLayout)
{
    setObjectName(QStringLiteral("card"));
    setStyleSheet(QStringLiteral("QFrame#card { background: palette(base); border: 1px solid palette(mid);"
                                 " border-radius: 10px; }"));
    m_avatar->setFixedSize(56, 56);
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.45);
    tf.setBold(true);
    m_title->setFont(tf);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_title->setWordWrap(true);
    QPalette muted = m_subtitle->palette();
    muted.setColor(QPalette::WindowText, muted.color(QPalette::Disabled, QPalette::WindowText));
    m_subtitle->setPalette(muted);
    m_subtitle->setWordWrap(true);
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::RichText);

    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(m_title, 1);
    titleRow->addWidget(m_pill, 0, Qt::AlignTop);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    text->addLayout(titleRow);
    text->addWidget(m_subtitle);
    text->addSpacing(4);
    text->addWidget(m_details);
    m_buttons->setContentsMargins(0, 6, 0, 0);
    m_buttons->addStretch();
    text->addLayout(m_buttons);
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(14, 12, 14, 12);
    l->setSpacing(14);
    l->addWidget(m_avatar, 0, Qt::AlignTop);
    l->addLayout(text, 1);
}

void Header::setRecord(const EntityDef &def, const QVariantMap &rec, int activeLoans, bool inMaintenance)
{
    const QString label = def.displayLabel(rec);
    m_title->setText(label);
    m_subtitle->setText(subtitle(def, rec));
    m_subtitle->setVisible(!m_subtitle->text().isEmpty());

    if (def.isEquipment) {
        const bool hs = rec.value(QStringLiteral("hors_service")).toInt() != 0;
        const QColor c = activeLoans     ? Charts::borrowed()
                         : inMaintenance ? Charts::inMaintenance()
                         : hs            ? Charts::outOfService()
                                         : Charts::available();
        m_avatar->setPixmap(kindIcon(def.kind).pixmap(40, 40));
        m_avatar->setAlignment(Qt::AlignCenter);
        m_avatar->setStyleSheet(QStringLiteral("background: %1; border-radius: 28px;")
                                    .arg(QColor(c.red(), c.green(), c.blue(), 40).name(QColor::HexArgb)));
        setPill(m_pill,
                activeLoans     ? QObject::tr("Emprunté")
                : inMaintenance ? QObject::tr("En entretien")
                : hs            ? QObject::tr("Hors service")
                                : QObject::tr("Disponible"),
                c);
    } else {
        const QDate end = QDate::fromString(rec.value(QStringLiteral("adhesion_fin")).toString(), Qt::ISODate);
        QColor c = accent();
        QString pill;
        if (Schema::isInactive(rec)) {
            c = neutral();
            pill = QObject::tr("Désactivé");
        } else if (end.isValid() && end < QDate::currentDate()) {
            c = danger();
            pill = QObject::tr("Adhésion expirée");
        } else if (activeLoans) {
            c = Charts::borrowed();
            pill = QObject::tr("%n emprunt(s)", nullptr, activeLoans);
        } else {
            c = success();
            pill = QObject::tr("Actif");
        }
        m_avatar->setStyleSheet(QString());
        m_avatar->setPixmap(avatar(initials(label), Schema::isInactive(rec) ? neutral() : accent(), 56));
        setPill(m_pill, pill, c);
    }
    fitHeight();
}

void Header::fitHeight()
{
    if (layout() && layout()->hasHeightForWidth()) {
        const int h = layout()->totalHeightForWidth(width());
        if (h != minimumHeight())
            setMinimumHeight(h);
    }
}

void Header::addButton(QWidget *button)
{
    m_buttons->insertWidget(m_buttons->count() - 1, button);
    fitHeight();
}

void Header::setDetails(const QString &html)
{
    m_details->setText(html);
    m_details->setVisible(!html.isEmpty());
    fitHeight();
}

void Header::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    fitHeight();
}

void Header::setEmpty(const QString &kind, const QString &text)
{
    m_avatar->setStyleSheet(QString());
    m_avatar->setPixmap(kindIcon(kind).pixmap(40, 40));
    m_avatar->setAlignment(Qt::AlignCenter);
    m_title->setText(text);
    m_subtitle->clear();
    m_subtitle->hide();
    m_details->clear();
    setPill(m_pill, QString(), neutral());
    fitHeight();
}

} // namespace Ui
