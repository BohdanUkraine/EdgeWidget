#pragma once

#include <QHash>
#include <QObject>
#include <QTranslator>

// UI language. The sources are written in Ukrainian (tr("...") strings); other languages
// are a dictionary from the Ukrainian text, installed as a QTranslator so every tr() call
// switches at once without restarting. Qt's own dialogs (colour picker, About Qt) are English
// in the sources: in Ukrainian they get Qt's translation (translations/qtbase_uk.qm).
//
// "auto" picks the language from the country of the Spotify account (Spotify does not
// expose the account's UI language), falling back to the Windows language.
class Localization : public QObject
{
    Q_OBJECT

public:
    static Localization &instance();

    // Language codes offered in the settings, with their native names.
    static QList<QPair<QString, QString>> languages();

    void setSpotifyCountry(const QString &country); // ISO code, e.g. "UA"
    void apply();                                    // re-evaluate after a setting change

signals:
    void languageChanged();

private:
    class Dictionary : public QTranslator
    {
    public:
        QHash<QString, QString> strings;
        QString translate(const char *context, const char *sourceText, const char *disambiguation,
                          int n) const override;
        bool isEmpty() const override { return strings.isEmpty(); }
    };

    Localization();
    QString resolve() const;

    QString m_country;
    QString m_current = QStringLiteral("uk");
    Dictionary m_english;
    QTranslator m_qtUkrainian;
    bool m_applied = false;
};
