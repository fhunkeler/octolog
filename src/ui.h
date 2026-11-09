#pragma once

#include "schema.h"

#include <QColor>
#include <QFrame>
#include <QIcon>

class QHBoxLayout;
class QLabel;

// Elements visuels communs : icones, couleurs, pastilles, avatars et cartes.
// Les widgets restent natifs ; seules les cartes et pastilles sont stylees.
namespace Ui {

QIcon icon(const QString &name);       // :/icons/<name>.svg
QIcon kindIcon(const QString &kind);   // icone d'un type (membres, blocs...)

QColor accent();   // bleu du club
QColor orange();
QColor success();
QColor danger();
QColor neutral();

QIcon dot(const QColor &color);        // pastille ronde pour les listes
QPixmap avatar(const QString &text, const QColor &color, int size);
QString initials(const QString &label);

// Pastille de texte coloree (etat).
void setPill(QLabel *label, const QString &text, const QColor &color);

// Cadre arrondi sur fond "base" (cartes du tableau de bord, en-tete de fiche).
QFrame *card(QWidget *parent = nullptr);

// Separation horizontale entre la zone d'actions et les donnees.
QWidget *separator(QWidget *parent = nullptr);

// Resume d'une fiche sur une ligne ("15 L · Air · TIV 26/10/2024").
QString subtitle(const EntityDef &def, const QVariantMap &rec);

// En-tete de fiche : avatar, titre, resume, pastille d'etat et ligne d'informations.
class Header : public QFrame
{
public:
    explicit Header(QWidget *parent = nullptr);
    void setRecord(const EntityDef &def, const QVariantMap &rec, int activeLoans, bool inMaintenance = false);
    void setEmpty(const QString &kind, const QString &text);
    QLabel *details() const { return m_details; }
    void setDetails(const QString &html);
    // Ajoute un bouton sous le texte de la carte.
    void addButton(QWidget *button);

protected:
    // Les textes renvoyes a la ligne agrandissent la carte au lieu d'etre coupes.
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitHeight();

    QLabel *m_avatar;
    QLabel *m_title;
    QLabel *m_subtitle;
    QLabel *m_pill;
    QLabel *m_details;
    QHBoxLayout *m_buttons;
};

} // namespace Ui
