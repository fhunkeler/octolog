#pragma once

#include "schema.h"

#include <QDialog>
#include <QHash>

// Formulaire de creation / modification genere a partir d'une EntityDef.
class RecordDialog : public QDialog
{
    Q_OBJECT
public:
    RecordDialog(const EntityDef &def, const QVariantMap &record, QWidget *parent = nullptr);
    QVariantMap values() const;

    void accept() override;

private:
    const EntityDef &m_def;
    QHash<QString, QWidget *> m_editors;
};
