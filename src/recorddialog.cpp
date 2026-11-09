#include "recorddialog.h"

#include "database.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

namespace {

// Date facultative : case a cocher + calendrier.
class OptionalDate : public QWidget
{
public:
    explicit OptionalDate(QWidget *parent)
        : QWidget(parent)
        , check(new QCheckBox(this))
        , edit(new QDateEdit(this))
    {
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(check);
        l->addWidget(edit);
        l->addStretch();
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
        edit->setDate(QDate::currentDate());
        edit->setEnabled(false);
        check->setToolTip(QObject::tr("Date renseignée"));
        QObject::connect(check, &QCheckBox::toggled, edit, &QWidget::setEnabled);
    }
    void setValue(const QVariant &v)
    {
        const QDate d = QDate::fromString(v.toString(), Qt::ISODate);
        check->setChecked(d.isValid());
        if (d.isValid())
            edit->setDate(d);
    }
    QVariant value() const
    {
        return check->isChecked() ? QVariant(edit->date().toString(Qt::ISODate)) : QVariant();
    }
    QCheckBox *check;
    QDateEdit *edit;
};

QStringList distinctValues(const EntityDef &def, const QString &key)
{
    QSet<QString> seen;
    QStringList out;
    for (const QVariantMap &rec : Database::instance().records(def)) {
        const QString v = rec.value(key).toString().trimmed();
        if (!v.isEmpty() && !seen.contains(v)) {
            seen.insert(v);
            out << v;
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

} // namespace

RecordDialog::RecordDialog(const EntityDef &def, const QVariantMap &record, QWidget *parent)
    : QDialog(parent)
    , m_def(def)
{
    const bool creating = record.isEmpty();
    setWindowTitle(creating ? tr("Nouveau %1").arg(def.singular)
                            : tr("Modifier %1 %2").arg(def.singular, def.displayLabel(record)));

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const Field &f : def.fields) {
        const QVariant v = record.value(f.key);
        QWidget *editor = nullptr;
        switch (f.type) {
        case FieldType::LongText: {
            auto *e = new QPlainTextEdit(this);
            e->setPlainText(v.toString());
            e->setTabChangesFocus(true);
            e->setFixedHeight(e->fontMetrics().height() * 4 + 12);
            editor = e;
            break;
        }
        case FieldType::Date: {
            auto *e = new OptionalDate(this);
            e->setValue(v);
            editor = e;
            break;
        }
        case FieldType::Bool: {
            auto *e = new QCheckBox(this);
            e->setChecked(v.toInt() != 0);
            editor = e;
            break;
        }
        case FieldType::Choice: {
            auto *e = new QComboBox(this);
            e->setEditable(true);
            QStringList items = f.choices;
            for (const QString &s : distinctValues(def, f.key))
                if (!items.contains(s))
                    items << s;
            e->addItem(QString());
            e->addItems(items);
            e->setCurrentText(v.toString());
            editor = e;
            break;
        }
        default: {
            auto *e = new QLineEdit(v.isNull() ? QString() : Schema::formatValue(f, v), this);
            if (f.type == FieldType::Text) {
                auto *c = new QCompleter(distinctValues(def, f.key), e);
                c->setCaseSensitivity(Qt::CaseInsensitive);
                c->setFilterMode(Qt::MatchContains);
                e->setCompleter(c);
            } else {
                e->setPlaceholderText(f.type == FieldType::Integer ? tr("nombre entier") : tr("nombre"));
            }
            editor = e;
            break;
        }
        }
        m_editors.insert(f.key, editor);
        form->addRow(f.label + QStringLiteral(" :"), editor);
    }

    auto *content = new QWidget;
    content->setLayout(form);
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(creating ? tr("Créer") : tr("Enregistrer"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);

    content->adjustSize();
    resize(520, qMin(content->sizeHint().height() + 80, 720));
}

QVariantMap RecordDialog::values() const
{
    QVariantMap out;
    for (const Field &f : m_def.fields) {
        QWidget *w = m_editors.value(f.key);
        QVariant v;
        if (auto *e = dynamic_cast<OptionalDate *>(w))
            v = e->value();
        else if (auto *e = qobject_cast<QPlainTextEdit *>(w))
            v = Schema::parseValue(f, e->toPlainText());
        else if (auto *e = qobject_cast<QCheckBox *>(w))
            v = e->isChecked() ? 1 : 0;
        else if (auto *e = qobject_cast<QComboBox *>(w))
            v = Schema::parseValue(f, e->currentText());
        else if (auto *e = qobject_cast<QLineEdit *>(w))
            v = Schema::parseValue(f, e->text());
        out.insert(f.key, v);
    }
    return out;
}

void RecordDialog::accept()
{
    const QVariantMap v = values();
    // Champ identifiant obligatoire
    const QString keyField = m_def.isEquipment ? m_def.codeField : QStringLiteral("nom");
    if (v.value(keyField).toString().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Le champ « %1 » est obligatoire.").arg(m_def.field(keyField)->label));
        m_editors.value(keyField)->setFocus();
        return;
    }
    for (const Field &f : m_def.fields) {
        if (f.type != FieldType::Integer && f.type != FieldType::Real)
            continue;
        const QVariant x = v.value(f.key);
        if (x.isValid() && x.typeId() == QMetaType::QString) {
            QMessageBox::warning(this, windowTitle(), tr("« %1 » doit être un nombre.").arg(f.label));
            m_editors.value(f.key)->setFocus();
            return;
        }
    }
    QDialog::accept();
}
