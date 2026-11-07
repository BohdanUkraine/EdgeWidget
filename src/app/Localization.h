#pragma once

#include <QHash>
#include <QObject>
#include <QTranslator>

class Localization : public QObject
{
    Q_OBJECT

public:
    static Localization &instance();

    static QList<QPair<QString, QString>> languages();

    void setSpotifyCountry(const QString &country);
    void apply();

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
