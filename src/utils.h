#pragma once
#include <QDir>
#include <QList>
#include <QMap>
#include <QString>

namespace Achroma
{

struct QuickLink
{
    QString name;
    QString url;
    QString icon;
};

QString configDir();
QString dataDir();
QString formatUrl(const QString& input);
QString searchUrl(const QString& engine, const QString& query);
QString duckDuckGoUrl(const QString& query);
QString urlEncode(const QString& text);
QString stripTerminalControls(QString text);
QString homePageHtml(const QMap<QString, QString>& bookmarks = {}, const QList<QuickLink>& quickLinks = {});
QString markdownPageHtml(const QString& title, const QString& markdown);
QString linkHintsScript();
QString keyScrollScript();
QString codeBlockScript();
QString installCmdScript();

extern const QMap<QString, QString> builtinSearchEngines;

}  // namespace Achroma
