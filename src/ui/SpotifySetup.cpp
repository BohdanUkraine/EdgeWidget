#include "ui/SpotifySetup.h"

#include "spotify/SpotifyAuth.h"
#include "spotify/SpotifyClient.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

void runSpotifySetup(SpotifyClient *spotify, QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Підключення Spotify"));
    dlg.setMinimumWidth(460);

    const QStringList stepTexts = {
        QObject::tr("Відкрий <a href='https://developer.spotify.com/dashboard'>developer.spotify.com/dashboard</a> і натисни <b>Create app</b>."),
        QObject::tr("Назва та опис — будь-які. У <b>Redirect URIs</b> додай адресу нижче."),
        QObject::tr("У розділі API познач <b>Web API</b> і збережи."),
        QObject::tr("Скопіюй <b>Client ID</b> зі сторінки застосунку і встав сюди."),
    };
    auto *steps = new QLabel(QStringLiteral("<ol style='margin-left:-20px'><li>%1</li></ol>")
                                 .arg(stepTexts.join(QStringLiteral("</li><li>"))));
    steps->setOpenExternalLinks(true);
    steps->setWordWrap(true);

    auto *redirect = new QLineEdit(SpotifyAuth::redirectUri());
    redirect->setReadOnly(true);
    auto *copy = new QPushButton(QObject::tr("Копіювати"));
    QObject::connect(copy, &QPushButton::clicked, redirect, [redirect] {
        QApplication::clipboard()->setText(redirect->text());
    });
    auto *redirectRow = new QHBoxLayout;
    redirectRow->addWidget(redirect);
    redirectRow->addWidget(copy);

    auto *clientId = new QLineEdit(spotify->clientId());
    clientId->setPlaceholderText(QObject::tr("Client ID (32 символи)"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Увійти в Spotify"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(steps);
    layout->addWidget(new QLabel(QObject::tr("Redirect URI:")));
    layout->addLayout(redirectRow);
    layout->addWidget(new QLabel(QObject::tr("Client ID:")));
    layout->addWidget(clientId);
    layout->addSpacing(8);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString id = clientId->text().trimmed();
    if (id.isEmpty())
        return;
    spotify->setClientId(id);
    spotify->login();
}
