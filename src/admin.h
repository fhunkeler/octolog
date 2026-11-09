#pragma once

#include <QObject>

class QTimer;
class QWidget;

// Mode utilisateur / mode admin.
//
// Mode utilisateur (par defaut) : consultation, recherche, export, pret et retour.
// Mode admin (code PIN) : fiches adherents / materiel, entretiens, cautions,
// import, changement de base, suppressions d'historique.
// Le code PIN est stocke chiffre (SHA-256 sale) dans la base : il suit le fichier.
// Retour automatique en mode utilisateur apres une periode d'inactivite.
class Admin : public QObject
{
    Q_OBJECT
public:
    static Admin &instance();

    bool isAdmin() const { return m_admin; }
    bool hasPin() const;

    // Demande le code PIN (ou le fait definir la premiere fois) et passe en mode admin.
    bool unlock(QWidget *parent);
    void lock();
    // Vrai si deja admin ; sinon demande le code PIN.
    static bool require(QWidget *parent);
    bool changePin(QWidget *parent);
    // Ligne de commande : efface le code (il sera redefini au prochain passage en admin).
    static void resetPin();
    // Ligne de commande : definit le code (4 a 6 chiffres). Retourne faux si invalide.
    static bool setPin(const QString &pin);
    // Ligne de commande (--admin) : mode admin sans code PIN.
    void forceAdmin();

    // Affiche le widget seulement en mode admin.
    static void adminOnly(QWidget *w);
    static void adminOnly(class QAction *a);

    static constexpr int IdleMinutes = 10;

signals:
    void changed(bool admin);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Admin();
    bool askNewPin(QWidget *parent);
    static QString hash(const QString &pin, const QString &salt);

    bool m_admin = false;
    QTimer *m_idle;
};
