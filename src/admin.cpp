#include "admin.h"

#include "database.h"
#include "ui.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const QString PinHashKey = QStringLiteral("admin_pin_hash");
const QString PinSaltKey = QStringLiteral("admin_pin_salt");

QLineEdit *pinEdit(QWidget *parent)
{
    auto *e = new QLineEdit(parent);
    e->setEchoMode(QLineEdit::Password);
    e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,6}")), e));
    e->setPlaceholderText(QObject::tr("4 à 6 chiffres"));
    e->setMaxLength(6);
    QFont f = e->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    e->setFont(f);
    return e;
}

// Fenetre de saisie : un ou deux champs PIN.
QStringList askPins(QWidget *parent, const QString &title, const QString &text, const QStringList &labels)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setWindowIcon(Ui::icon(QStringLiteral("lock")));
    auto *head = new Ui::Header(&dlg);
    head->setEmpty(QStringLiteral("lock"), title);
    head->setDetails(text);
    auto *form = new QFormLayout;
    QList<QLineEdit *> edits;
    for (const QString &l : labels) {
        edits << pinEdit(&dlg);
        form->addRow(l, edits.last());
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *l = new QVBoxLayout(&dlg);
    l->addWidget(head);
    l->addLayout(form);
    l->addWidget(buttons);
    dlg.resize(420, dlg.sizeHint().height());
    edits.first()->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return {};
    QStringList out;
    for (QLineEdit *e : edits)
        out << e->text();
    return out;
}
} // namespace

Admin &Admin::instance()
{
    static Admin a;
    return a;
}

Admin::Admin()
    : m_idle(new QTimer(this))
{
    m_idle->setSingleShot(true);
    m_idle->setInterval(IdleMinutes * 60 * 1000);
    connect(m_idle, &QTimer::timeout, this, &Admin::lock);
    // Toute activite clavier / souris relance le delai d'inactivite.
    qApp->installEventFilter(this);
    // Changement de base : retour en mode utilisateur
    connect(&Database::instance(), &Database::opened, this, &Admin::lock);
}

bool Admin::eventFilter(QObject *watched, QEvent *event)
{
    if (m_admin) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            m_idle->start();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

QString Admin::hash(const QString &pin, const QString &salt)
{
    return QString::fromLatin1(QCryptographicHash::hash((salt + QLatin1Char(':') + pin).toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}

bool Admin::hasPin() const
{
    return !Database::instance().setting(PinHashKey).isEmpty();
}

bool Admin::askNewPin(QWidget *parent)
{
    for (;;) {
        const QStringList pins = askPins(parent, tr("Définir le code PIN admin"),
                                         tr("Choisissez un code de 4 à 6 chiffres. Il sera demandé pour passer en mode admin "
                                            "(fiches, entretiens, cautions, import)."),
                                         {tr("Nouveau code :"), tr("Confirmation :")});
        if (pins.isEmpty())
            return false;
        if (pins[0].size() < 4) {
            QMessageBox::warning(parent, tr("Code PIN"), tr("Le code doit comporter au moins 4 chiffres."));
            continue;
        }
        if (pins[0] != pins[1]) {
            QMessageBox::warning(parent, tr("Code PIN"), tr("Les deux codes ne sont pas identiques."));
            continue;
        }
        return setPin(pins[0]);
    }
}

bool Admin::unlock(QWidget *parent)
{
    if (m_admin)
        return true;
    if (!hasPin()) {
        if (!askNewPin(parent))
            return false;
    } else {
        const QStringList pin = askPins(parent, tr("Mode admin"),
                                        tr("Saisissez le code PIN pour modifier les données "
                                           "(fiches, entretiens, cautions, import)."),
                                        {tr("Code PIN :")});
        if (pin.isEmpty())
            return false;
        if (hash(pin.first(), Database::instance().setting(PinSaltKey)) != Database::instance().setting(PinHashKey)) {
            QMessageBox::warning(parent, tr("Mode admin"), tr("Code PIN incorrect."));
            return false;
        }
    }
    m_admin = true;
    m_idle->start();
    emit changed(true);
    return true;
}

void Admin::lock()
{
    if (!m_admin)
        return;
    m_admin = false;
    m_idle->stop();
    emit changed(false);
}

bool Admin::require(QWidget *parent)
{
    return instance().isAdmin() || instance().unlock(parent);
}

bool Admin::changePin(QWidget *parent)
{
    if (!require(parent))
        return false;
    if (!askNewPin(parent))
        return false;
    QMessageBox::information(parent, tr("Code PIN"), tr("Le code PIN admin a été modifié."));
    return true;
}

void Admin::resetPin()
{
    Database::instance().setSetting(PinHashKey, QString());
    Database::instance().setSetting(PinSaltKey, QString());
}

bool Admin::setPin(const QString &pin)
{
    static const QRegularExpression valid(QStringLiteral("^[0-9]{4,6}$"));
    if (!valid.match(pin).hasMatch())
        return false;
    const QString salt = QString::number(QRandomGenerator::global()->generate64(), 16);
    Database::instance().setSetting(PinSaltKey, salt);
    Database::instance().setSetting(PinHashKey, hash(pin, salt));
    return true;
}

void Admin::forceAdmin()
{
    if (m_admin)
        return;
    m_admin = true;
    m_idle->start();
    emit changed(true);
}

void Admin::adminOnly(QWidget *w)
{
    if (!w)
        return;
    w->setVisible(instance().isAdmin());
    connect(&instance(), &Admin::changed, w, &QWidget::setVisible);
}

void Admin::adminOnly(QAction *a)
{
    if (!a)
        return;
    a->setVisible(instance().isAdmin());
    connect(&instance(), &Admin::changed, a, &QAction::setVisible);
}
