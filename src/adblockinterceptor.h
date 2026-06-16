#pragma once
#include <QSet>
#include <QString>
#include <QWebEngineUrlRequestInterceptor>

class AdBlockInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    explicit AdBlockInterceptor(QObject* parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
    void loadBlocklist(const QString& path);
    bool isEnabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool on)
    {
        m_enabled = on;
    }
    bool matchesDomain(const QUrl& url) const;

private:
    QSet<QString> m_blockedDomains;
    bool m_enabled = true;
};
