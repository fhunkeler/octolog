#include "xlsx.h"

#include <QDate>
#include <QHash>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

namespace {

int columnIndex(QStringView ref)
{
    int col = 0;
    for (QChar c : ref) {
        if (!c.isLetter())
            break;
        col = col * 26 + (c.toUpper().unicode() - 'A' + 1);
    }
    return col - 1;
}

QString columnName(int index)
{
    QString s;
    for (int n = index + 1; n > 0; n = (n - 1) / 26)
        s.prepend(QChar('A' + (n - 1) % 26));
    return s;
}

// Concatene le texte des <t> sous l'element courant (ignore les annotations phonetiques).
QString readRichText(QXmlStreamReader &xml, const QString &endTag)
{
    QString text;
    int phonetic = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("rPh"))
                ++phonetic;
            else if (xml.name() == QLatin1String("t") && phonetic == 0)
                text += xml.readElementText();
        } else if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("rPh"))
                --phonetic;
            else if (xml.name() == endTag)
                break;
        }
    }
    return text;
}

QString resolveTarget(const QString &target)
{
    if (target.startsWith('/'))
        return target.mid(1);
    return QStringLiteral("xl/") + target;
}

} // namespace

namespace Xlsx {

QList<Sheet> read(const QString &path, QString *error)
{
    QZipReader zip(path);
    if (!zip.isReadable()) {
        if (error)
            *error = QStringLiteral("Impossible d'ouvrir %1").arg(path);
        return {};
    }

    QStringList shared;
    {
        QXmlStreamReader xml(zip.fileData(QStringLiteral("xl/sharedStrings.xml")));
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("si"))
                shared << readRichText(xml, QStringLiteral("si"));
        }
    }

    QHash<QString, QString> rels;
    {
        QXmlStreamReader xml(zip.fileData(QStringLiteral("xl/_rels/workbook.xml.rels")));
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("Relationship"))
                rels.insert(xml.attributes().value("Id").toString(),
                            resolveTarget(xml.attributes().value("Target").toString()));
        }
    }

    QList<QPair<QString, QString>> sheets; // nom, chemin
    {
        QXmlStreamReader xml(zip.fileData(QStringLiteral("xl/workbook.xml")));
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("sheet")) {
                QString rid;
                for (const auto &a : xml.attributes())
                    if (a.name() == QLatin1String("id"))
                        rid = a.value().toString();
                sheets.append({xml.attributes().value("name").toString(), rels.value(rid)});
            }
        }
    }

    QList<Sheet> out;
    for (const auto &[name, file] : sheets) {
        Sheet sheet;
        sheet.name = name;
        QXmlStreamReader xml(zip.fileData(file));
        int rowIndex = -1;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (xml.name() == QLatin1String("row")) {
                const int r = xml.attributes().value("r").toInt();
                rowIndex = r > 0 ? r - 1 : rowIndex + 1;
                while (sheet.rows.size() <= rowIndex)
                    sheet.rows.append(QStringList());
            } else if (xml.name() == QLatin1String("c") && rowIndex >= 0) {
                const auto attrs = xml.attributes();
                const QString type = attrs.value("t").toString();
                int col = attrs.hasAttribute("r") ? columnIndex(attrs.value("r"))
                                                  : sheet.rows[rowIndex].size();
                QString value;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name() == QLatin1String("c"))
                        break;
                    if (!xml.isStartElement())
                        continue;
                    if (xml.name() == QLatin1String("v"))
                        value = xml.readElementText();
                    else if (xml.name() == QLatin1String("is"))
                        value = readRichText(xml, QStringLiteral("is"));
                }
                if (type == QLatin1String("s"))
                    value = shared.value(value.toInt());
                else if (type == QLatin1String("b"))
                    value = value == QLatin1String("1") ? QStringLiteral("Oui") : QStringLiteral("Non");
                QStringList &row = sheet.rows[rowIndex];
                while (row.size() <= col)
                    row.append(QString());
                row[col] = value;
            }
        }
        if (xml.hasError() && error)
            *error = xml.errorString();
        out << sheet;
    }
    return out;
}

namespace {

QByteArray sheetXml(const QStringList &headers, const QList<QVariantList> &rows)
{
    QByteArray sheet;
    QXmlStreamWriter x(&sheet);
    x.writeStartDocument(QStringLiteral("1.0"), true);
    x.writeStartElement(QStringLiteral("worksheet"));
    x.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    // Ligne d'en-tete figee
    x.writeStartElement(QStringLiteral("sheetViews"));
    x.writeStartElement(QStringLiteral("sheetView"));
    x.writeAttribute(QStringLiteral("workbookViewId"), QStringLiteral("0"));
    x.writeEmptyElement(QStringLiteral("pane"));
    x.writeAttribute(QStringLiteral("ySplit"), QStringLiteral("1"));
    x.writeAttribute(QStringLiteral("topLeftCell"), QStringLiteral("A2"));
    x.writeAttribute(QStringLiteral("state"), QStringLiteral("frozen"));
    x.writeEndElement();
    x.writeEndElement();
    // Largeur de colonnes approximative d'apres le contenu
    if (!headers.isEmpty()) {
        x.writeStartElement(QStringLiteral("cols"));
        for (int c = 0; c < headers.size(); ++c) {
            int w = int(headers[c].size());
            for (int r = 0; r < rows.size() && r < 200; ++r)
                w = qMax(w, int(rows[r].value(c).toString().size()));
            x.writeEmptyElement(QStringLiteral("col"));
            x.writeAttribute(QStringLiteral("min"), QString::number(c + 1));
            x.writeAttribute(QStringLiteral("max"), QString::number(c + 1));
            x.writeAttribute(QStringLiteral("width"), QString::number(qBound(8, w + 2, 60)));
            x.writeAttribute(QStringLiteral("customWidth"), QStringLiteral("1"));
        }
        x.writeEndElement();
    }
    x.writeStartElement(QStringLiteral("sheetData"));

    auto writeRow = [&](int r, const QVariantList &values, bool header) {
        x.writeStartElement(QStringLiteral("row"));
        x.writeAttribute(QStringLiteral("r"), QString::number(r + 1));
        for (int c = 0; c < values.size(); ++c) {
            const QVariant &v = values[c];
            if (!v.isValid() || v.isNull() || v.toString().isEmpty())
                continue;
            x.writeStartElement(QStringLiteral("c"));
            x.writeAttribute(QStringLiteral("r"), columnName(c) + QString::number(r + 1));
            const int typeId = v.typeId();
            if (typeId == QMetaType::QDate || typeId == QMetaType::QDateTime) {
                const QDateTime dt = typeId == QMetaType::QDate ? v.toDate().startOfDay() : v.toDateTime();
                const double serial = QDate(1899, 12, 30).daysTo(dt.date()) + dt.time().msecsSinceStartOfDay() / 86400000.0;
                x.writeAttribute(QStringLiteral("s"), typeId == QMetaType::QDate ? QStringLiteral("2") : QStringLiteral("3"));
                x.writeTextElement(QStringLiteral("v"), QString::number(serial, 'f', 6));
            } else if (typeId == QMetaType::Int || typeId == QMetaType::Double || typeId == QMetaType::LongLong) {
                x.writeTextElement(QStringLiteral("v"), QString::number(v.toDouble(), 'g', 15));
            } else {
                x.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                if (header)
                    x.writeAttribute(QStringLiteral("s"), QStringLiteral("1"));
                x.writeStartElement(QStringLiteral("is"));
                x.writeStartElement(QStringLiteral("t"));
                x.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                x.writeCharacters(v.toString());
                x.writeEndElement();
                x.writeEndElement();
            }
            x.writeEndElement();
        }
        x.writeEndElement();
    };
    QVariantList head;
    for (const QString &h : headers)
        head << h;
    writeRow(0, head, true);
    for (int r = 0; r < rows.size(); ++r)
        writeRow(r + 1, rows[r], false);
    x.writeEndElement(); // sheetData
    if (!headers.isEmpty()) {
        x.writeEmptyElement(QStringLiteral("autoFilter"));
        x.writeAttribute(QStringLiteral("ref"),
                         QStringLiteral("A1:%1%2").arg(columnName(int(headers.size()) - 1)).arg(rows.size() + 1));
    }
    x.writeEndElement(); // worksheet
    x.writeEndDocument();
    return sheet;
}

QString safeSheetName(const QString &name, QStringList *used)
{
    QString n = name.left(31);
    for (QChar c : QStringLiteral("[]:*?/\\"))
        n.replace(c, QLatin1Char('_'));
    if (n.isEmpty())
        n = QStringLiteral("Feuille");
    QString unique = n;
    for (int i = 2; used->contains(unique, Qt::CaseInsensitive); ++i)
        unique = n.left(28) + QStringLiteral(" %1").arg(i);
    used->append(unique);
    return unique;
}

} // namespace

bool writeWorkbook(const QString &path, const QList<SheetData> &sheets, QString *error)
{
    QStringList used;
    QStringList names;
    for (const SheetData &sh : sheets)
        names << safeSheetName(sh.name, &used);

    QByteArray contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    for (int i = 0; i < sheets.size(); ++i)
        contentTypes += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/"
                                       "vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                            .arg(i + 1)
                            .toUtf8();
    contentTypes += "</Types>";

    const QByteArray rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";

    QByteArray workbook;
    {
        QXmlStreamWriter x(&workbook);
        x.writeStartDocument(QStringLiteral("1.0"), true);
        x.writeStartElement(QStringLiteral("workbook"));
        x.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
        x.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        x.writeStartElement(QStringLiteral("sheets"));
        for (int i = 0; i < sheets.size(); ++i) {
            x.writeEmptyElement(QStringLiteral("sheet"));
            x.writeAttribute(QStringLiteral("name"), names[i]);
            x.writeAttribute(QStringLiteral("sheetId"), QString::number(i + 1));
            x.writeAttribute(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
                             QStringLiteral("id"), QStringLiteral("rId%1").arg(i + 1));
        }
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndDocument();
    }

    QByteArray workbookRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (int i = 0; i < sheets.size(); ++i)
        workbookRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
                                       "2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>")
                            .arg(i + 1)
                            .toUtf8();
    workbookRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
                                   "2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>")
                        .arg(sheets.size() + 1)
                        .toUtf8();

    // Styles : 0 normal, 1 en-tete gras, 2 date, 3 date + heure.
    const QByteArray styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"dd/mm/yyyy\"/>"
        "<numFmt numFmtId=\"165\" formatCode=\"dd/mm/yyyy hh:mm\"/></numFmts>"
        "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"4\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>";

    QZipWriter zip(path, QIODevice::WriteOnly);
    if (zip.status() != QZipWriter::NoError) {
        if (error)
            *error = QStringLiteral("Impossible d'écrire %1").arg(path);
        return false;
    }
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(QStringLiteral("[Content_Types].xml"), contentTypes);
    zip.addFile(QStringLiteral("_rels/.rels"), rootRels);
    zip.addFile(QStringLiteral("xl/workbook.xml"), workbook);
    zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRels);
    zip.addFile(QStringLiteral("xl/styles.xml"), styles);
    for (int i = 0; i < sheets.size(); ++i)
        zip.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), sheetXml(sheets[i].headers, sheets[i].rows));
    zip.close();
    if (zip.status() != QZipWriter::NoError) {
        if (error)
            *error = QStringLiteral("Erreur lors de l'écriture de %1").arg(path);
        return false;
    }
    return true;
}

bool write(const QString &path, const QString &sheetName, const QStringList &headers,
           const QList<QVariantList> &rows, QString *error)
{
    return writeWorkbook(path, {{sheetName, headers, rows}}, error);
}

} // namespace Xlsx
