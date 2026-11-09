#include "schema.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

const Field *EntityDef::field(const QString &key) const
{
    for (const Field &f : fields)
        if (f.key == key)
            return &f;
    return nullptr;
}

int EntityDef::fieldIndex(const QString &key) const
{
    for (int i = 0; i < fields.size(); ++i)
        if (fields[i].key == key)
            return i;
    return -1;
}

QString EntityDef::displayLabel(const QVariantMap &rec) const
{
    QString out = labelPattern;
    static const QRegularExpression re(QStringLiteral("\\{(\\w+)\\}"));
    auto it = re.globalMatch(labelPattern);
    while (it.hasNext()) {
        const auto m = it.next();
        const Field *f = field(m.captured(1));
        const QString value = f ? Schema::formatValue(*f, rec.value(f->key)) : QString();
        out.replace(m.captured(0), value);
    }
    return out.trimmed();
}

namespace {

Field F(const QString &key, const QString &label, FieldType type, bool inTable,
        const QStringList &imports, const QStringList &choices = {})
{
    Field f;
    f.key = key;
    f.label = label;
    f.type = type;
    f.inTable = inTable;
    f.importHeaders = imports;
    f.choices = choices;
    return f;
}

constexpr auto T = FieldType::Text;
constexpr auto L = FieldType::LongText;
constexpr auto I = FieldType::Integer;
constexpr auto R = FieldType::Real;
constexpr auto D = FieldType::Date;
constexpr auto C = FieldType::Choice;
constexpr auto B = FieldType::Bool;

EntityDef makeMembers()
{
    EntityDef d;
    d.kind = QStringLiteral("membres");
    d.table = QStringLiteral("membres");
    d.label = QStringLiteral("Adhérents");
    d.singular = QStringLiteral("adhérent");
    d.labelPattern = QStringLiteral("{nom} {prenom}");
    d.importKeys = {{"id_externe"}, {"email"}, {"nom", "prenom"}};
    d.fields = {
        F("nom", "Nom", T, true, {"last_name", "Nom"}),
        F("prenom", "Prénom", T, true, {"first_name", "Prénom"}),
        F("email", "Email", T, false, {"email", "Courriel"}),
        F("telephone", "Téléphone", T, true, {"osm_telephone_mobile", "Téléphone", "phone"}),
        F("telephone_fixe", "Tél. fixe", T, false, {"phone"}),
        F("telephone_pro", "Tél. professionnel", T, false, {"osm_telephone_professionnel"}),
        F("email2", "Email secondaire", T, false, {"osm_Courriel_Secondaire"}),
        F("adresse", "Adresse", T, false, {"address", "Adresse"}),
        F("code_postal", "Code postal", T, false, {"zip", "Code postal"}),
        F("ville", "Ville", T, false, {"city", "Ville"}),
        F("date_naissance", "Date de naissance", D, false, {"osm_Date_de_naissance"}),
        F("lieu_naissance", "Lieu de naissance", T, false, {"birthcity"}),
        F("activite", "Activité", C, false, {"plan"}, {"Plongée", "Nage Avec Palmes"}),
        F("niveau_prepare", "Niveau préparé", T, false, {"osm_Niveau_prepare"}),
        F("carte_niveau", "Carte niveau", C, false, {"osm_carte_niveau"}, {"Oui", "Non"}),
        F("pret_bloc", "Prêt bloc", C, false, {"osm_Pret_Bloc"}, {"Non", "Oui", "Oui (Encadrant)"}),
        F("pret_detendeur", "Prêt détendeur", C, false, {"osm_Pret_Detendeur"}, {"Non", "Oui"}),
        F("pret_gilet", "Prêt gilet", C, false, {"osm_Pret_Gilet"}, {"Non", "Oui"}),
        F("assurance", "Assurance", C, false, {"osm_Assurance_Individuelle"},
          {"Non", "Loisir 1", "Loisir 2", "Loisir 3"}),
        F("piscine", "Piscine", C, false, {"osm_Piscine"}, {"Oui", "Non"}),
        F("origine", "Origine adhésion", T, false, {"osm_Origine_Adhesion"}),
        F("adhesion_debut", "Début adhésion", D, false, {"from_date"}),
        F("adhesion_fin", "Fin d'adhésion", D, false, {"to_date"}),
        F("identifiant", "Identifiant site", T, false, {"username"}),
        F("id_externe", "ID utilisateur", T, false, {"user_id"}),
        F("num_adhesion", "N° adhésion", T, false, {"membership_id"}),
        F("commentaire", "Commentaire", L, false, {"comment", "Commentaire"}),
        F("desactive", "Désactivé", B, false, {}),
    };
    return d;
}

EntityDef makeBlocs()
{
    EntityDef d;
    d.kind = QStringLiteral("blocs");
    d.table = QStringLiteral("blocs");
    d.label = QStringLiteral("Blocs");
    d.singular = QStringLiteral("bloc");
    d.isEquipment = true;
    d.labelPattern = QStringLiteral("{nom}");
    d.codeField = QStringLiteral("nom");
    d.rightField = QStringLiteral("pret_bloc");
    d.importKeys = {{"nom"}};
    d.fields = {
        F("nom", "Nom", T, true, {"Nom"}),
        F("capacite", "Capacité (L)", R, true, {"Capacité(L)", "Capacité"}),
        F("gaz", "Gaz", C, true, {"Gaz"}, {"Air", "Nitrox", "Oxy"}),
        F("marque", "Marque bloc", T, false, {"Marque Bloc"}),
        F("fabricant", "Fabricant", T, false, {"Fabricant"}),
        F("numero_serie", "N° de série", T, false, {"Numéro de série"}),
        F("proprietaire", "Propriétaire", T, false, {"Propriétaire"}),
        F("date_inspection", "Dernière inspection (TIV)", D, true, {"Date d'inspection"}),
        F("date_requalif", "Dernière requalification", D, false, {"Date de dernière requalification"}),
        F("inspecteur", "Inspecteur", T, false, {"Nom inspecteur"}),
        F("statut", "Statut", T, false, {"Statut"}),
        F("pe", "PE (bar)", R, false, {"PE (bar)"}),
        F("ps", "PS (bar)", R, false, {"PS(bar)", "PS (bar)"}),
        F("filetage_bloc", "Filetage bloc", T, false, {"Filetage Bloc"}),
        F("date_premiere_epreuve", "Date 1ère épreuve", D, false, {"Date 1ère épreuve"}),
        F("marque_robinet", "Marque robinet", T, false, {"Marque Robinet"}),
        F("numero_robinet", "N° robinet", T, false, {"Numéro Robinet"}),
        F("filetage_robinet", "Filetage robinet", T, false, {"Filetage Robinet"}),
        F("action", "Dernière action", T, false, {"Action"}),
        F("regime", "Régime", C, false, {"Régime"}, {"Dérogatoire (TIV)", "Standard"}),
        F("materiau", "Matériau", C, false, {"Matériau"}, {"Acier", "Aluminium", "Composite"}),
        F("hors_service", "Hors service", B, false, {"Hors service"}),
        F("commentaire", "Commentaire", L, false, {"Commentaire"}),
    };
    return d;
}

EntityDef makeDetendeurs()
{
    EntityDef d;
    d.kind = QStringLiteral("detendeurs");
    d.table = QStringLiteral("detendeurs");
    d.label = QStringLiteral("Détendeurs");
    d.singular = QStringLiteral("détendeur");
    d.isEquipment = true;
    d.labelPattern = QStringLiteral("{identifiant} - {type}");
    d.codeField = QStringLiteral("identifiant");
    d.rightField = QStringLiteral("pret_detendeur");
    d.importKeys = {{"identifiant"}};
    d.fields = {
        F("identifiant", "N°", I, true, {"Identifiant"}),
        F("type", "Type", C, true, {"Type"}, {"Complet", "Moniteur"}),
        F("marque", "Marque", T, true, {"Marque"}),
        F("modele", "Modèle", T, true, {"Modèle"}),
        F("ref_1er", "Réf. 1er étage", T, false, {"Ref 1er"}),
        F("ref_2e", "Réf. 2e étage", T, false, {"Ref Sec"}),
        F("ref_octo", "Réf. octopus", T, false, {"Ref Octo"}),
        F("etat", "État", T, false, {"Etat"}),
        F("date_revision", "Dernière révision", D, false, {"Date de dernière révision"}),
        F("lieu", "Emplacement", T, false, {"Présent Local"}),
        F("hors_service", "Hors service", B, false, {"Hors service"}),
        F("commentaire", "Commentaire", L, false, {"Commentaire"}),
    };
    return d;
}

EntityDef makeGilets()
{
    EntityDef d;
    d.kind = QStringLiteral("gilets");
    d.table = QStringLiteral("gilets");
    d.label = QStringLiteral("Gilets");
    d.singular = QStringLiteral("gilet");
    d.isEquipment = true;
    d.labelPattern = QStringLiteral("{identifiant} - {taille}");
    d.codeField = QStringLiteral("identifiant");
    d.rightField = QStringLiteral("pret_gilet");
    d.importKeys = {{"identifiant"}};
    d.fields = {
        F("identifiant", "N°", I, true, {"Identifiant"}),
        F("taille", "Taille", C, true, {"Taille"}, {"XXS", "XS", "S", "M", "L", "XL", "XXL"}),
        F("marque", "Marque", T, true, {"Marque"}),
        F("modele", "Modèle", T, true, {"Modèle"}),
        F("taille_num", "Taille (n°)", I, false, {"Taille AN"}),
        F("annee", "Année", I, false, {"Année"}),
        F("etat", "État", T, false, {"Etat"}),
        F("lieu", "Emplacement", T, false, {"Lieu"}),
        F("verifie_par", "Vérifié par", T, false, {"Vérifié par"}),
        F("date_verif", "Date vérification", D, false, {"Date vérif"}),
        F("resultat_verif", "Résultat vérification", T, false, {"Etat 2024/25"}),
        F("hors_service", "Hors service", B, false, {"Hors service"}),
        F("commentaire", "Commentaire", L, false, {"Commentaire vérification", "Commentaire"}),
    };
    return d;
}

} // namespace

namespace Schema {

const EntityDef &members()
{
    static const EntityDef d = makeMembers();
    return d;
}

const QList<const EntityDef *> &equipment()
{
    static const EntityDef blocs = makeBlocs();
    static const EntityDef detendeurs = makeDetendeurs();
    static const EntityDef gilets = makeGilets();
    static const QList<const EntityDef *> all = {&blocs, &detendeurs, &gilets};
    return all;
}

const EntityDef *byKind(const QString &kind)
{
    if (kind == members().kind)
        return &members();
    for (const EntityDef *d : equipment())
        if (d->kind == kind)
            return d;
    return nullptr;
}

QDate parseDate(const QString &input)
{
    const QString s = input.trimmed();
    if (s.isEmpty())
        return {};
    static const QStringList formats = {
        "dd/MM/yyyy HH:mm:ss", "dd/MM/yyyy HH:mm", "dd/MM/yyyy", "d/M/yyyy",
        "yyyy-MM-dd", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm:ss", "dd.MM.yyyy"};
    for (const QString &fmt : formats) {
        const QDate d = fmt.contains(QLatin1Char('H')) ? QDateTime::fromString(s, fmt).date()
                                                        : QDate::fromString(s, fmt);
        if (d.isValid())
            return d;
    }
    // numero de serie Excel (jours depuis le 30/12/1899)
    bool ok = false;
    const double serial = s.toDouble(&ok);
    if (ok && serial > 1 && serial < 100000)
        return QDate(1899, 12, 30).addDays(qint64(serial));
    return {};
}

QString formatValue(const Field &f, const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return {};
    switch (f.type) {
    case FieldType::Date: {
        const QDate d = QDate::fromString(v.toString(), Qt::ISODate);
        return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : v.toString();
    }
    case FieldType::Bool:
        return v.toInt() ? QStringLiteral("Oui") : QString();
    case FieldType::Real: {
        bool ok = false;
        const double x = v.toDouble(&ok);
        return ok ? QLocale().toString(x, 'g', 10) : v.toString();
    }
    default:
        return v.toString();
    }
}

QVariant sortValue(const Field &f, const QVariant &v)
{
    if (!v.isValid() || v.isNull() || v.toString().isEmpty())
        return {};
    switch (f.type) {
    case FieldType::Date:
        return QDate::fromString(v.toString(), Qt::ISODate);
    case FieldType::Integer:
    case FieldType::Real:
    case FieldType::Bool:
        return v.toDouble();
    default:
        return v.toString();
    }
}

QVariant parseValue(const Field &f, const QString &input)
{
    const QString s = input.trimmed();
    if (s.isEmpty())
        return {};
    switch (f.type) {
    case FieldType::Date: {
        const QDate d = parseDate(s);
        return d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant(s);
    }
    case FieldType::Integer: {
        bool ok = false;
        const double x = QString(s).replace(',', '.').toDouble(&ok);
        return ok ? QVariant(qint64(x)) : QVariant(s);
    }
    case FieldType::Real: {
        bool ok = false;
        const double x = QString(s).replace(',', '.').toDouble(&ok);
        return ok ? QVariant(x) : QVariant(s);
    }
    case FieldType::Bool:
        return isYes(s) || s == QLatin1String("1") ? 1 : 0;
    default:
        return s;
    }
}

bool isYes(const QVariant &v)
{
    const QString s = normalize(v.toString());
    return s.startsWith(QLatin1String("oui")) || s == QLatin1String("1") || s == QLatin1String("true");
}

QString normalize(const QString &input)
{
    const QString decomposed = input.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    bool lastSpace = false;
    for (QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        if (c.isSpace()) {
            if (!lastSpace && !out.isEmpty())
                out += QLatin1Char(' ');
            lastSpace = true;
            continue;
        }
        lastSpace = false;
        out += c.toLower();
    }
    return out.trimmed();
}

} // namespace Schema
