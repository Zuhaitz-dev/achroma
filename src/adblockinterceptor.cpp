#include "adblockinterceptor.h"
#include <QFile>
#include <QRegularExpression>
#include <QUrl>
#include <QWebEngineUrlRequestInfo>

AdBlockInterceptor::AdBlockInterceptor(QObject* parent) : QWebEngineUrlRequestInterceptor(parent)
{
}

void AdBlockInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    if (!m_enabled)
    {
        return;
    }
    if (info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame)
    {
        return;
    }
    if (matchesDomain(info.requestUrl()))
    {
        info.block(true);
    }
}

bool AdBlockInterceptor::matchesDomain(const QUrl& url) const
{
    const QString host = url.host();
    for (const QString& domain : m_blockedDomains)
    {
        if (host == domain || host.endsWith("." + domain))
        {
            return true;
        }
    }
    return false;
}

void AdBlockInterceptor::loadBlocklist(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return;
    }
    int count = 0;
    while (!f.atEnd() && count < 100000)
    {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('!') || line.startsWith('#'))
        {
            continue;
        }
        if (line.startsWith("||") && line.endsWith('^'))
        {
            m_blockedDomains.insert(line.mid(2, line.length() - 3));
        }
        else
        {
            m_blockedDomains.insert(line);
        }
        count++;
    }
    f.close();
}
