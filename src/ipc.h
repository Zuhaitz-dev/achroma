#pragma once
#include <QObject>

class QLocalServer;
class CommandDispatcher;
class BrowserTabs;
class Terminal;

class IPCServer : public QObject
{
    Q_OBJECT
public:
    explicit IPCServer(QObject* parent = nullptr);

    void setDispatcher(CommandDispatcher* cmd)
    {
        m_dispatcher = cmd;
    }
    void setBrowser(BrowserTabs* browser)
    {
        m_browser = browser;
    }
    void setTerminal(Terminal* terminal)
    {
        m_terminal = terminal;
    }

    bool start(const QString& sockPath);
    QString socketPath() const
    {
        return m_sockPath;
    }

private:
    void handleConnection();

    QLocalServer* m_server = nullptr;
    QString m_sockPath;
    CommandDispatcher* m_dispatcher = nullptr;
    BrowserTabs* m_browser = nullptr;
    Terminal* m_terminal = nullptr;
};
