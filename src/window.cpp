#include "window.h"
#include "adblockinterceptor.h"
#include "browser.h"
#include "commands.h"
#include "fuzzyfinder.h"
#include "ipc.h"
#include "terminal.h"
#include "triggers.h"
#include "utils.h"
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QtGlobal>
#include <qtermwidget.h>
#include <chrono>

namespace
{
void prepareLocalBrowserView(QWebEngineView* view)
{
    if (!view)
        return;
    view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    view->settings()->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
#endif
}

void showPdfRenderError(QWebEngineView* view, const QString& path, const QString& detail)
{
    if (!view)
        return;
    const QString html =
        "<html><body style='background:#0b0b0b;color:#ddd;font-family:monospace;padding:24px;'>"
        "<h2 style='color:#fff'>PDF preview unavailable</h2>"
        "<p>Achroma kept this inside the tab, but could not render:</p>"
        "<pre style='white-space:pre-wrap'>" +
        path.toHtmlEscaped() +
        "</pre>"
        "<p style='color:#aaa'>" +
        detail.toHtmlEscaped() +
        "</p>"
        "</body></html>";
    view->setHtml(html, QUrl("achroma://pdf-error"));
}

void openPdfInBrowser(QWebEngineView* view, const QString& path)
{
    if (!view)
        return;
    prepareLocalBrowserView(view);

    const QString renderer = QStandardPaths::findExecutable("pdftoppm");
    if (renderer.isEmpty())
    {
        showPdfRenderError(
            view, path, "Install poppler-utils/pdftoppm to preview PDFs inside Achroma on this Qt build."
        );
        return;
    }

    const QFileInfo info(path);
    const QString key =
        info.canonicalFilePath() + QString::number(info.size()) + info.lastModified().toString(Qt::ISODateWithMs);
    QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheBase.isEmpty())
        cacheBase = QDir::tempPath() + "/achroma";
    const QString cacheRoot = cacheBase + "/pdf-preview/" + QString::number(qHash(key), 16);
    QDir().mkpath(cacheRoot);

    QDir outDir(cacheRoot);
    auto sortPdfPages = [](QStringList& files)
    {
        std::sort(
            files.begin(),
            files.end(),
            [](const QString& a, const QString& b)
            {
                static const QRegularExpression re("-(\\d+)\\.png$");
                const auto ma = re.match(a);
                const auto mb = re.match(b);
                if (ma.hasMatch() && mb.hasMatch())
                    return ma.captured(1).toInt() < mb.captured(1).toInt();
                return a < b;
            }
        );
    };
    QStringList pages = outDir.entryList({"page-*.png"}, QDir::Files, QDir::Name);
    sortPdfPages(pages);
    if (pages.isEmpty())
    {
        QProcess render;
        render.setProgram(renderer);
        render.setArguments({"-png", "-r", "144", path, cacheRoot + "/page"});
        render.setProcessChannelMode(QProcess::MergedChannels);
        render.start();
        if (!render.waitForFinished(30000) || render.exitStatus() != QProcess::NormalExit || render.exitCode() != 0)
        {
            showPdfRenderError(view, path, QString::fromUtf8(render.readAll()).trimmed());
            return;
        }
        pages = outDir.entryList({"page-*.png"}, QDir::Files, QDir::Name);
        sortPdfPages(pages);
    }

    if (pages.isEmpty())
    {
        showPdfRenderError(view, path, "The PDF renderer finished without producing pages.");
        return;
    }

    QString html =
        "<html><body style='margin:0;background:#202124;color:#ddd;font-family:sans-serif;'>"
        "<div style='position:sticky;top:0;background:#111;padding:8px 14px;border-bottom:1px solid #333;'>" +
        info.fileName().toHtmlEscaped() +
        "</div>"
        "<main style='display:flex;flex-direction:column;align-items:center;gap:18px;padding:18px;'>";
    for (const QString& page : pages)
    {
        html += "<img src='" + QUrl::fromLocalFile(cacheRoot + "/" + page).toString().toHtmlEscaped() +
                "' style='max-width:100%;height:auto;background:white;box-shadow:0 2px 12px rgba(0,0,0,.45);'>";
    }
    html += "</main></body></html>";
    view->setHtml(html, QUrl::fromLocalFile(path));
}

void openLocalFileInBrowser(QWebEngineView* view, const QString& path)
{
    if (!view)
        return;
    if (QFileInfo(path).suffix().compare("pdf", Qt::CaseInsensitive) == 0)
        openPdfInBrowser(view, path);
    else
    {
        prepareLocalBrowserView(view);
        view->setUrl(QUrl::fromLocalFile(path));
    }
}
}  // namespace

AchromaWindow::AchromaWindow(QWidget* parent) : QMainWindow(parent)
{
    setupUi();
    setupShortcuts();
}

AchromaWindow::~AchromaWindow() = default;

void AchromaWindow::setupUi()
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    setWindowTitle("achroma");
    resize(1200, 800);

    m_central = new QWidget(this);
    setCentralWidget(m_central);

    QVBoxLayout* mainLayout = new QVBoxLayout(m_central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_titleBar = new QWidget(m_central);
    m_titleBar->setFixedHeight(26);
    m_titleBar->setStyleSheet("QWidget { background: #060606; border-bottom: 1px solid #141414; }");
    m_titleBar->installEventFilter(this);
    m_titleBar->setMouseTracking(true);

    auto* tbLayout = new QHBoxLayout(m_titleBar);
    tbLayout->setContentsMargins(0, 0, 0, 0);
    tbLayout->setSpacing(0);

    auto* btnCluster = new QWidget(m_titleBar);
    btnCluster->setFixedWidth(74);
    btnCluster->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    btnCluster->setStyleSheet("QWidget { background: transparent; border: none; }");
    auto* clusterLayout = new QHBoxLayout(btnCluster);
    clusterLayout->setContentsMargins(14, 0, 14, 0);
    clusterLayout->setSpacing(7);
    clusterLayout->setAlignment(Qt::AlignVCenter);

    auto makeDot = [&](const QString& idleColor, const QString& hoverColor)
    {
        auto* btn = new QToolButton(btnCluster);
        btn->setFixedSize(10, 10);
        btn->setCursor(Qt::ArrowCursor);
        btn->setStyleSheet(QString(
                               "QToolButton { background: %1; border: none; border-radius: 5px; }"
                               "QToolButton:hover { background: %2; }"
        )
                               .arg(idleColor, hoverColor));
        clusterLayout->addWidget(btn);
        return btn;
    };

    auto* closeBtn = makeDot("#2a1212", "#e05050");
    auto* minBtn = makeDot("#1c1c0a", "#d4aa30");
    auto* maxBtn = makeDot("#0a1c0a", "#3db554");

    tbLayout->addWidget(btnCluster);

    m_titleLabel = new QLabel(m_titleBar);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        "color: #383838; font-family: monospace; font-size: 10px; "
        "letter-spacing: 1px; border: none;"
    );
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tbLayout->addWidget(m_titleLabel);

    auto* tbRightSpacer = new QWidget(m_titleBar);
    tbRightSpacer->setFixedWidth(74);
    tbRightSpacer->setStyleSheet("background: transparent; border: none;");
    auto* audioLayout = new QHBoxLayout(tbRightSpacer);
    audioLayout->setContentsMargins(8, 0, 14, 0);
    audioLayout->setSpacing(0);
    m_audioIndicator = new QLabel(tbRightSpacer);
    m_audioIndicator->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_audioIndicator->setFixedWidth(52);
    m_audioIndicator->setStyleSheet(
        "color: #303030; font-family: monospace; font-size: 10px; letter-spacing: 1px; border: none;"
    );
    audioLayout->addWidget(m_audioIndicator);
    tbLayout->addWidget(tbRightSpacer);

    m_audioTimer = new QTimer(this);
    m_audioTimer->setInterval(180);
    connect(m_audioTimer, &QTimer::timeout, this, &AchromaWindow::tickTitleAudioIndicator);

    connect(closeBtn, &QToolButton::clicked, this, &AchromaWindow::close);
    connect(minBtn, &QToolButton::clicked, this, &AchromaWindow::showMinimized);
    connect(maxBtn, &QToolButton::clicked, this, [this]() { isMaximized() ? showNormal() : showMaximized(); });

    mainLayout->addWidget(m_titleBar);

    m_splitter = new QSplitter(Qt::Vertical, m_central);

    m_browser = new BrowserTabs(m_splitter);
    m_splitter->addWidget(m_browser->container());

    m_devToolsBar = new QWidget(m_splitter);
    m_devToolsBar->setMinimumHeight(80);
    m_devToolsBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* dtLayout = new QVBoxLayout(m_devToolsBar);
    dtLayout->setContentsMargins(0, 0, 0, 0);
    dtLayout->setSpacing(0);

    auto* dtHeader = new QWidget(m_devToolsBar);
    dtHeader->setFixedHeight(22);
    dtHeader->setStyleSheet(
        "QWidget { background: #0a0a0a; border-top: 1px solid #1e1e1e; border-bottom: 1px solid #141414; }"
    );
    auto* dtHLayout = new QHBoxLayout(dtHeader);
    dtHLayout->setContentsMargins(10, 0, 4, 0);
    dtHLayout->setSpacing(0);
    auto* dtTitle = new QLabel("devtools", dtHeader);
    dtTitle->setStyleSheet(
        "color: #383838; font-family: monospace; font-size: 10px; letter-spacing: 2px; border: none;"
    );
    dtHLayout->addWidget(dtTitle);
    dtHLayout->addStretch();
    auto* dtClose = new QToolButton(dtHeader);
    dtClose->setText("×");
    dtClose->setFixedSize(22, 22);
    dtClose->setStyleSheet(
        "QToolButton { color: #383838; background: transparent; border: none; font-size: 14px; font-family: monospace; "
        "} QToolButton:hover { color: #e05050; }"
    );
    dtHLayout->addWidget(dtClose);

    m_devToolsView = new QWebEngineView(m_devToolsBar);
    dtLayout->addWidget(dtHeader);
    dtLayout->addWidget(m_devToolsView);
    m_devToolsBar->hide();
    m_splitter->addWidget(m_devToolsBar);

    connect(dtClose, &QToolButton::clicked, this, [this]() { openDevTools(); });

    m_terminal = new Terminal(m_splitter);
    m_termContainer = new QWidget(m_splitter);
    QVBoxLayout* termLayout = new QVBoxLayout(m_termContainer);
    termLayout->setContentsMargins(0, 0, 0, 0);
    termLayout->setSpacing(0);
    termLayout->addWidget(m_terminal->widget());
    m_splitter->addWidget(m_termContainer);

    m_browser->container()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_termContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, true);
    m_splitter->setCollapsible(2, false);

    mainLayout->addWidget(m_splitter);

    m_dispatcher = new CommandDispatcher(this);
    m_dispatcher->setBrowser(m_browser);
    m_dispatcher->setTerminal(m_terminal);
    m_dispatcher->showHelpCallback = [this]()
    {
        showHelp();
    };
    m_dispatcher->showHintsCallback = [this]()
    {
        showLinkHints();
    };
    m_dispatcher->fullscreenCallback = [this]()
    {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    };

    m_dispatcher->fuzzyCallback = [this]()
    {
        m_finder->toggle();
    };

    const AppConfig& cfg = m_dispatcher->appearance();
    m_browser->applyAppearance(cfg);
    m_terminal->applyAppearance(cfg);
    setStyleSheet(QString("background-color: %1; color: %2;").arg(cfg.bg, cfg.fg));
    m_splitter->setStyleSheet(QString(
                                  "QSplitter::handle { background-color: %1; } "
                                  "QSplitter::handle:horizontal { width: 1px; } "
                                  "QSplitter::handle:vertical { height: 1px; }"
    )
                                  .arg(cfg.fg));

    if (!cfg.qssFile.isEmpty())
    {
        QString themesDir = Achroma::configDir() + "/themes";
        QDir().mkpath(themesDir);
        QString fullPath = QDir::cleanPath(cfg.qssFile.startsWith('/') ? cfg.qssFile : themesDir + "/" + cfg.qssFile);
        QString canonical = QFileInfo(fullPath).canonicalFilePath();
        QString canonicalThemes = QFileInfo(themesDir).canonicalFilePath();
        if (!canonical.isEmpty() && canonical.startsWith(canonicalThemes))
        {
            QFile qss(canonical);
            if (qss.open(QIODevice::ReadOnly))
                qApp->setStyleSheet(qApp->styleSheet() + QString::fromUtf8(qss.readAll()));
        }
    }

    m_triggers = new TriggerEngine(this);
    m_triggers->setBrowser(m_browser);
    m_triggers->setTriggers(m_dispatcher->triggers());
    connect(m_terminal, &Terminal::outputAvailable, m_triggers, &TriggerEngine::processTerminalOutput);
    connect(m_terminal, &Terminal::shellExited, this, []() { QApplication::quit(); });
    connect(
        m_dispatcher,
        &CommandDispatcher::configChanged,
        this,
        [this]()
        {
            m_triggers->setTriggers(m_dispatcher->triggers());
            setupHelpOverlay();
        }
    );

    m_terminal->start();
    qApp->installEventFilter(this);

    setupHelpOverlay();

    m_finder = new FuzzyFinder(m_central);
    m_finder->setBrowser(m_browser);
    m_finder->setDispatcher(m_dispatcher);
    m_finder->setTerminal(m_terminal);
    m_finder->hide();
    connect(m_finder, &FuzzyFinder::closed, m_finder, &QWidget::hide);

    m_statusLabel = new QLabel(m_browser->container());
    m_statusLabel->setStyleSheet(
        "QLabel { background-color: #000000; color: #999999; font-family: monospace; "
        "font-size: 11px; padding: 2px 8px; border-top: 1px solid #333333; }"
    );
    m_statusLabel->setFixedHeight(20);
    qobject_cast<QVBoxLayout*>(m_browser->container()->layout())->addWidget(m_statusLabel);

    connect(
        m_browser,
        &BrowserTabs::linkHovered,
        this,
        [this](const QString& url) { m_statusLabel->setText(url.isEmpty() ? QString() : " " + url); }
    );

    connect(m_browser, &BrowserTabs::statusMessage, this, [this](const QString& msg) { setStatus(msg, 5000); });

    connect(
        m_browser,
        &BrowserTabs::titleChanged,
        this,
        [this](const QString& title)
        {
            setWindowTitle(title.isEmpty() ? "achroma" : title);
            if (m_titleLabel)
                m_titleLabel->setText(title.length() > 72 ? title.left(70) + u8"…" : title);
        }
    );

    connect(m_browser, &BrowserTabs::audibleChanged, this, &AchromaWindow::setTitleAudioActive);

    connect(
        m_browser->tabWidget(),
        &QTabWidget::currentChanged,
        this,
        [this](int)
        {
            if (!m_devToolsBar || !m_devToolsBar->isVisible())
                return;
            if (QWebEngineView* v = m_browser->currentView())
                attachDevToolsTo(v);
        }
    );

    m_ipc = new IPCServer(this);
    m_ipc->setDispatcher(m_dispatcher);
    m_ipc->setBrowser(m_browser);
    m_ipc->setTerminal(m_terminal);
    QString sockPath = Achroma::dataDir() + "/achroma.sock";
    QDir().mkpath(QFileInfo(sockPath).absolutePath());
    if (!m_ipc->start(sockPath))
        setStatus("IPC socket failed — CLI commands unavailable", 0);

    m_adBlocker = new AdBlockInterceptor(this);
    QString blocklistPath = Achroma::configDir() + "/blocklist.txt";
    m_adBlocker->loadBlocklist(blocklistPath);
    QWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(m_adBlocker);
    QWebEngineProfile::defaultProfile()->setSpellCheckEnabled(true);

    m_dispatcher->setAdBlockEnabled(m_adBlocker->isEnabled());
    m_dispatcher->adBlockToggleCallback = [this](bool on)
    {
        if (m_adBlocker)
        {
            m_adBlocker->setEnabled(on);
            setStatus(QString("Ad block %1").arg(on ? "on" : "off"), 2000);
        }
    };

    connect(
        QWebEngineProfile::defaultProfile(),
        &QWebEngineProfile::downloadRequested,
        this,
        [this](QWebEngineDownloadRequest* dl)
        {
            const QUrl requested = dl->url();
            if (requested.isLocalFile() &&
                QFileInfo(requested.toLocalFile()).suffix().compare("pdf", Qt::CaseInsensitive) == 0)
            {
                dl->cancel();
                openLocalFileInBrowser(m_browser ? m_browser->currentView() : nullptr, requested.toLocalFile());
                return;
            }

            QString safeName = QFileInfo(dl->downloadFileName()).fileName();
            QString path =
                QFileDialog::getSaveFileName(nullptr, "Save Download", dl->downloadDirectory() + "/" + safeName);
            if (path.isEmpty())
                return;
            dl->setDownloadDirectory(QFileInfo(path).path());
            dl->setDownloadFileName(QFileInfo(path).fileName());
            dl->accept();

            QString label = " ↓ " + QFileInfo(path).fileName();
            m_statusLabel->setText(label);
            connect(
                dl,
                &QWebEngineDownloadRequest::receivedBytesChanged,
                this,
                [this, dl, label]()
                {
                    qint64 total = dl->totalBytes();
                    if (total > 0)
                        m_statusLabel->setText(label + QString(" %1%").arg(dl->receivedBytes() * 100 / total));
                }
            );
            connect(
                dl,
                &QWebEngineDownloadRequest::isFinishedChanged,
                this,
                [this, dl, path, label]()
                {
                    if (dl->state() == QWebEngineDownloadRequest::DownloadCompleted)
                    {
                        m_statusLabel->setText(" ✓ " + label.mid(2));
                        QTimer::singleShot(4000, m_statusLabel, [this]() { m_statusLabel->setText(QString()); });
                        if (m_terminal)
                            m_terminal->sendText("# downloaded: " + path + "\n");
                    }
                    else if (dl->state() == QWebEngineDownloadRequest::DownloadInterrupted)
                    {
                        m_statusLabel->setText(" ✗ " + label.mid(2));
                        QTimer::singleShot(4000, m_statusLabel, [this]() { m_statusLabel->setText(QString()); });
                    }
                }
            );
        }
    );
}

void AchromaWindow::setupShortcuts()
{
    const KeyConfig& keys = m_dispatcher->keyConfig();

    connect(
        new QShortcut(QKeySequence(keys.focusTerminal), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_termContainer && !m_termContainer->isVisible())
            {
                m_termContainer->show();
                QList<int> sizes = m_splitter->sizes();
                int restoreH = m_savedTermH > 0 ? m_savedTermH : m_splitter->height() / 3;
                int last = sizes.size() - 1;
                sizes[0] = qMax(100, sizes[0] - restoreH);
                sizes[last] = restoreH;
                m_splitter->setSizes(sizes);
            }
            m_terminal->setFocus();
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.toggleTerminal), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (!m_termContainer)
                return;
            if (m_termContainer->isVisible())
            {
                m_savedTermH = m_splitter->sizes().last();
                m_termContainer->hide();
                m_browser->urlBar()->setFocus();
            }
            else
            {
                m_termContainer->show();
                QList<int> sizes = m_splitter->sizes();
                int restoreH = m_savedTermH > 0 ? m_savedTermH : m_splitter->height() / 3;
                int last = sizes.size() - 1;
                sizes[0] = qMax(100, sizes[0] - restoreH);
                sizes[last] = restoreH;
                m_splitter->setSizes(sizes);
                m_terminal->setFocus();
            }
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.focusUrlbar), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            m_browser->urlBar()->setFocus();
            m_browser->urlBar()->selectAll();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.toggleSplit), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            m_splitter->setOrientation(m_splitter->orientation() == Qt::Vertical ? Qt::Horizontal : Qt::Vertical);

            int total = m_splitter->orientation() == Qt::Horizontal ? m_splitter->width() : m_splitter->height();
            int devH = (m_devToolsBar && m_devToolsBar->isVisible()) ? total / 4 : 0;
            m_splitter->setSizes({total * 2 / 3 - devH, devH, total / 3});
        }
    );

    auto* newTabShortcut = new QShortcut(QKeySequence(keys.newTabFromUrlbar), m_browser->urlBar());
    newTabShortcut->setContext(Qt::WidgetShortcut);
    connect(
        newTabShortcut,
        &QShortcut::activated,
        this,
        [this]()
        {
            QString input = m_browser->urlBar()->text().trimmed();
            if (!input.isEmpty())
                m_browser->addNewTab(QUrl(Achroma::formatUrl(input)));
            m_browser->urlBar()->clear();
        }
    );

    for (int i = 1; i <= 9; i++)
    {
        connect(
            new QShortcut(QKeySequence("Ctrl+Shift+" + QString::number(i)), this),
            &QShortcut::activated,
            this,
            [this, i]()
            {
                if (i <= m_browser->tabCount())
                    m_browser->tabWidget()->setCurrentIndex(i - 1);
            }
        );
    }

    for (int i = 1; i <= 8; i++)
    {
        connect(
            new QShortcut(QKeySequence("Ctrl+" + QString::number(i)), this),
            &QShortcut::activated,
            this,
            [this, i]()
            {
                if (m_browser && i <= m_browser->tabCount())
                    m_browser->switchToTab(i - 1);
            }
        );
    }
    connect(
        new QShortcut(QKeySequence("Ctrl+9"), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_browser)
                m_browser->switchToTab(m_browser->tabCount() - 1);
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.copySelection), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            QWebEngineView* v = m_browser->currentView();
            if (v)
                v->page()->runJavaScript(
                    "window.getSelection().toString();",
                    [this](const QVariant& s) { m_terminal->sendText(s.toString()); }
                );
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.linkHints), this), &QShortcut::activated, this, [this]() { showLinkHints(); }
    );
    connect(new QShortcut(QKeySequence(keys.showHelp), this), &QShortcut::activated, this, [this]() { showHelp(); });

    connect(
        new QShortcut(QKeySequence(keys.closeHelp), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_helpFrame->isVisible())
            {
                m_helpFrame->hide();
                return;
            }
            if (m_finder->isVisible())
            {
                m_finder->closeFinder();
                return;
            }
            if (m_browser)
                m_browser->setFindBarVisible(false);
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.fuzzyFinder), this),
        &QShortcut::activated,
        this,
        [this]() { m_finder->toggle(); }
    );

    connect(
        new QShortcut(QKeySequence(keys.reopenClosedTab), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_browser)
                m_browser->reopenLastTab();
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.nextTab), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_browser)
                m_browser->nextTab();
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.prevTab), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_browser)
                m_browser->prevTab();
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.findInPage), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_browser)
                m_browser->setFindBarVisible(true);
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.terminalCopy), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->copy();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.terminalPaste), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->paste();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.terminalClear), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->clear();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.terminalSearch), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->toggleSearchBar();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.terminalZoomIn), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->zoomIn();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.terminalZoomOut), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->zoomOut();
        }
    );
    connect(
        new QShortcut(QKeySequence(keys.terminalZoomReset), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_terminal)
                m_terminal->zoomReset();
        }
    );

    auto makeResizeShortcut = [this](const QKeySequence& key, int delta)
    {
        auto* sc = new QShortcut(key, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, delta]() { resizeTerminal(delta); });
    };
    makeResizeShortcut(QKeySequence(keys.resizeUp), 40);
    makeResizeShortcut(QKeySequence(keys.resizeDown), -40);
    makeResizeShortcut(QKeySequence(keys.resizeLeft), 40);
    makeResizeShortcut(QKeySequence(keys.resizeRight), -40);

    connect(
        new QShortcut(QKeySequence(keys.fullscreen), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
        }
    );

    auto* devToolsSc = new QShortcut(QKeySequence(keys.devTools), this);
    devToolsSc->setContext(Qt::ApplicationShortcut);
    connect(devToolsSc, &QShortcut::activated, this, &AchromaWindow::openDevTools);

    connect(
        new QShortcut(QKeySequence(keys.toggleDarkMode), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_browser)
                m_browser->toggleDarkMode();
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.toggleAdBlock), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            if (m_adBlocker)
            {
                m_adBlocker->setEnabled(!m_adBlocker->isEnabled());
                m_dispatcher->setAdBlockEnabled(m_adBlocker->isEnabled());
                setStatus(QString("Ad block %1").arg(m_adBlocker->isEnabled() ? "on" : "off"), 2000);
            }
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.viewSource), this),
        &QShortcut::activated,
        this,
        [this]()
        {
            QWebEngineView* v = m_browser->currentView();
            if (!v || !m_browser)
                return;
            QPointer<BrowserTabs> guard(m_browser);
            v->page()->toHtml(
                [guard](const QString& html)
                {
                    if (!guard)
                        return;
                    QWebEngineView* nv = new QWebEngineView();
                    nv->setHtml(
                        "<html><body "
                        "style='background:#000;color:#0f0;font-family:monospace;font-size:13px;padding:10px;white-"
                        "space:pre-wrap;'>" +
                            html.toHtmlEscaped() + "</body></html>",
                        QUrl("about:blank")
                    );
                    guard->tabWidget()->addTab(nv, "source");
                }
            );
        }
    );

    connect(
        new QShortcut(QKeySequence(keys.printPage), this),
        &QShortcut::activated,
        this,
        [this]() { m_dispatcher->execute("print"); }
    );

    connect(
        new QShortcut(QKeySequence(keys.toggleReader), this),
        &QShortcut::activated,
        this,
        [this]() { m_dispatcher->execute("reader"); }
    );

    connect(
        new QShortcut(QKeySequence(keys.toggleBmbar), this),
        &QShortcut::activated,
        this,
        [this]() { m_dispatcher->execute("bmbar"); }
    );

    connect(
        new QShortcut(QKeySequence(keys.sendToTerm), this),
        &QShortcut::activated,
        this,
        [this]() { m_dispatcher->execute("tosterm"); }
    );

    connect(
        new QShortcut(QKeySequence(keys.codeBlock), this),
        &QShortcut::activated,
        this,
        [this]() { m_dispatcher->execute("codeblock"); }
    );

    connect(
        new QShortcut(QKeySequence(keys.installCmd), this),
        &QShortcut::activated,
        this,
        [this]() { m_dispatcher->execute("install"); }
    );

    connect(m_browser, &BrowserTabs::urlBarReturnPressed, this, &AchromaWindow::onUrlBarReturnPressed);
}

void AchromaWindow::onUrlBarReturnPressed()
{
    QString input = m_browser->urlBar()->text().trimmed();
    if (input.isEmpty())
    {
        m_browser->urlBar()->clear();
        return;
    }
    if (input.startsWith(':'))
    {
        m_dispatcher->execute(input.mid(1));
    }
    else if (input == "home")
    {
        QWebEngineView* v = m_browser->currentView();
        if (v)
        {
            m_dispatcher->execute("home");
        }
    }
    else
    {
        QStringList words = input.split(' ', Qt::SkipEmptyParts);
        if (words.size() >= 2 && m_dispatcher->searchEngines().contains(words[0]))
        {
            m_dispatcher->execute(words[0] + " " + words.mid(1).join(' '));
        }
        else if (input.endsWith(".md", Qt::CaseInsensitive) || input.endsWith(".markdown", Qt::CaseInsensitive))
        {
            m_dispatcher->execute("md " + input);
        }
        else
        {
            QWebEngineView* v = m_browser->currentView();
            if (v && !m_browser->isPinned(m_browser->tabWidget()->currentIndex()))
                v->setUrl(QUrl(Achroma::formatUrl(input)));
            else
                m_browser->addNewTab(QUrl(Achroma::formatUrl(input)));
        }
    }
    m_browser->urlBar()->clearFocus();
    m_browser->urlBar()->clear();
}

void AchromaWindow::setupHelpOverlay()
{
    const bool wasVisible = m_helpFrame && m_helpFrame->isVisible();
    if (m_helpFrame)
    {
        m_helpFrame->deleteLater();
        m_helpFrame = nullptr;
    }

    m_helpFrame = new QFrame(m_central);
    m_helpFrame->setStyleSheet("QFrame { background-color: rgba(0, 0, 0, 220); }");

    auto* helpOuter = new QVBoxLayout(m_helpFrame);
    helpOuter->setAlignment(Qt::AlignCenter);
    helpOuter->setContentsMargins(24, 28, 24, 28);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMaximumWidth(920);
    scroll->setStyleSheet(
        "QScrollArea { background: #070707; border: 1px solid #181818; }"
        "QScrollBar:vertical { width: 4px; background: #070707; margin: 0; }"
        "QScrollBar::handle:vertical { background: #252525; border-radius: 2px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
    helpOuter->addWidget(scroll);

    auto* content = new QWidget();
    content->setStyleSheet(
        "QWidget { background: #070707; }"
        "QLabel  { border: none; color: #555; font-family: monospace; font-size: 11px; }"
    );

    auto* vbox = new QVBoxLayout(content);
    vbox->setContentsMargins(28, 22, 28, 20);
    vbox->setSpacing(0);

    auto* helpLogo = new QLabel(content);
    QPixmap helpPix(":/achroma.svg");
    if (!helpPix.isNull())
        helpLogo->setPixmap(helpPix.scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    helpLogo->setAlignment(Qt::AlignCenter);
    helpLogo->setStyleSheet("padding-bottom: 10px; padding-top: 4px;");
    vbox->addWidget(helpLogo);

    auto* helpTitle = new QLabel("A C H R O M A");
    helpTitle->setAlignment(Qt::AlignCenter);
    helpTitle->setStyleSheet(
        "color: #eee; font-size: 15px; font-weight: bold; "
        "letter-spacing: 8px; font-family: monospace; padding-bottom: 3px;"
    );
    vbox->addWidget(helpTitle);

    auto* helpSub = new QLabel("keyboard  &  command  reference");
    helpSub->setAlignment(Qt::AlignCenter);
    helpSub->setStyleSheet(
        "color: #555; font-size: 10px; letter-spacing: 2px; "
        "font-family: monospace; padding-bottom: 14px;"
    );
    vbox->addWidget(helpSub);

    auto* topSep = new QFrame(content);
    topSep->setFrameShape(QFrame::HLine);
    topSep->setStyleSheet("QFrame { color: #111; }");
    vbox->addWidget(topSep);

    const KeyConfig& kcfg = m_dispatcher->keyConfig();

    auto makeKey = [content](const QString& text)
    {
        auto* l = new QLabel(text, content);
        l->setStyleSheet(
            "color: #ccc; background: #0e0e0e; border: 1px solid #2a2a2a; "
            "border-radius: 2px; padding: 1px 7px; "
            "font-size: 11px; font-family: monospace;"
        );
        l->setFixedWidth(192);
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return l;
    };
    auto makeDesc = [content](const QString& text)
    {
        auto* l = new QLabel(text, content);
        l->setStyleSheet("color: #888; font-size: 11px; font-family: monospace;");
        return l;
    };

    using Entry = QPair<QString, QString>;
    auto addSection = [&](const QString& heading, const QVector<Entry>& entries)
    {
        auto* hdr = new QLabel(heading, content);
        hdr->setStyleSheet(
            "color: #555; font-size: 9px; letter-spacing: 4px; "
            "font-family: monospace; padding-top: 20px; padding-bottom: 6px;"
        );
        vbox->addWidget(hdr);

        auto* grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setVerticalSpacing(4);
        grid->setHorizontalSpacing(10);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        grid->setColumnMinimumWidth(2, 18);

        int half = (entries.size() + 1) / 2;
        for (int i = 0; i < entries.size(); i++)
        {
            int col = (i < half) ? 0 : 2;
            int row = (i < half) ? i : (i - half);
            grid->addWidget(makeKey(entries[i].first), row, col);
            grid->addWidget(makeDesc(entries[i].second), row, col + 1);
        }

        auto* gw = new QWidget(content);
        gw->setLayout(grid);
        vbox->addWidget(gw);
    };

    addSection(
        "NAVIGATION",
        {
            {":open  :o <url>", "Navigate current tab"},
            {":tab   :t <url>", "Open in new tab"},
            {":close :c", "Close current tab"},
            {":back  :b", "Go back"},
            {":forward :f", "Go forward"},
            {":r", "Reload page"},
            {":home", "Go to home dashboard"},
            {":undo  :u", "Reopen closed tab"},
            {":next  :n", "Next tab"},
            {":prev  :p", "Previous tab"},
            {":duplicate", "Duplicate current tab"},
            {":incognito", "New incognito tab"},
        }
    );

    QVector<Entry> searchEntries;
    searchEntries.append(Entry{":s <query>", "DuckDuckGo search"});
    for (auto it = m_dispatcher->searchEngines().constBegin(); it != m_dispatcher->searchEngines().constEnd(); ++it)
    {
        if (it.key() == "s" || it.key() == "ddg")
            continue;
        QString label = it.key() + " <query>";
        QString desc = it.value();
        desc.remove(QRegularExpression("https?://"));
        desc = desc.section('/', 0, 0);
        if (desc.isEmpty())
            desc = "Configured search engine";
        searchEntries.append(Entry{label, desc});
    }
    searchEntries.append(Entry{":bookmark :bm <n>", "Bookmark current page"});
    addSection("SEARCH", searchEntries);

    QVector<Entry> customEntries;
    for (auto it = m_dispatcher->customCommands().constBegin(); it != m_dispatcher->customCommands().constEnd(); ++it)
    {
        const QJsonObject cmd = it.value();
        const QString action = cmd["action"].toString("custom");
        customEntries.append(
            Entry{":" + it.key() + " <arg>", action.left(1).toUpper() + action.mid(1) + " custom command"}
        );
    }
    if (!customEntries.isEmpty())
        addSection("CUSTOM", customEntries);

    addSection(
        "KEYBOARD",
        {
            {kcfg.nextTab, "Next tab"},
            {kcfg.prevTab, "Previous tab"},
            {kcfg.reopenClosedTab, "Reopen closed tab"},
            {kcfg.focusUrlbar, "Focus URL bar"},
            {kcfg.focusTerminal, "Focus terminal"},
            {kcfg.findInPage, "Find in page"},
            {kcfg.linkHints, "Link hints overlay"},
            {kcfg.fuzzyFinder, "Fuzzy file / tab finder"},
            {kcfg.toggleReader, "Toggle reader mode"},
            {kcfg.toggleDarkMode, "Toggle dark mode"},
            {kcfg.toggleAdBlock, "Toggle ad blocking"},
            {kcfg.fullscreen, "Toggle fullscreen"},
            {kcfg.showHelp, "This window"},
        }
    );

    addSection(
        "TERMINAL",
        {
            {kcfg.toggleTerminal, "Toggle terminal"},
            {kcfg.terminalCopy, "Copy selection"},
            {kcfg.terminalPaste, "Paste"},
            {kcfg.terminalClear, "Clear"},
            {kcfg.terminalZoomIn, "Zoom in"},
            {kcfg.terminalZoomOut, "Zoom out"},
            {kcfg.terminalZoomReset, "Reset zoom"},
            {kcfg.sendToTerm, "Send selection to terminal"},
            {kcfg.codeBlock, "Extract code block"},
            {kcfg.installCmd, "Find install command"},
        }
    );

    addSection(
        "TOOLS",
        {
            {kcfg.devTools, "Developer tools"},
            {kcfg.viewSource, "View page source"},
            {kcfg.printPage, "Print / save as PDF"},
            {kcfg.toggleBmbar, "Toggle bookmark bar"},
            {":pipe <cmd>", "Run command, show output in tab"},
            {":reader", "Toggle reader mode"},
            {":dark / :light", "Toggle dark mode"},
            {":adblock", "Toggle ad blocking"},
            {":session save/load/list", "Session management"},
            {":history", "Recent URLs"},
            {":reload", "Reload config.json"},
        }
    );

    vbox->addStretch();

    auto* footerSep = new QFrame(content);
    footerSep->setFrameShape(QFrame::HLine);
    footerSep->setStyleSheet("QFrame { color: #111; margin-top: 12px; }");
    vbox->addWidget(footerSep);

    auto* footer = new QLabel("ESC  to close", content);
    footer->setAlignment(Qt::AlignCenter);
    footer->setStyleSheet(
        "color: #444; font-size: 10px; letter-spacing: 3px; "
        "font-family: monospace; padding: 10px 0 6px 0;"
    );
    vbox->addWidget(footer);

    scroll->setWidget(content);
    m_helpFrame->hide();

    if (wasVisible)
        showHelp();
}

void AchromaWindow::showHelp()
{
    m_helpFrame->resize(m_central->size());
    m_helpFrame->show();
    m_helpFrame->raise();
    m_helpFrame->setFocus();
}

void AchromaWindow::showLinkHints()
{
    QWebEngineView* v = m_browser->currentView();
    if (!v)
        return;
    v->page()->runJavaScript(Achroma::linkHintsScript());
}

void AchromaWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_titleBar)
        m_titleBar->setVisible(!isFullScreen());
}

void AchromaWindow::resizeTerminal(int delta)
{
    QList<int> sizes = m_splitter->sizes();
    if (sizes.size() < 2)
        return;
    int last = sizes.size() - 1;
    sizes[last] += delta;
    sizes[0] -= delta;
    if (sizes[0] < 100)
        sizes[0] = 100;
    if (sizes[last] < 100)
        sizes[last] = 100;
    m_splitter->setSizes(sizes);
}

void AchromaWindow::setTitleAudioActive(bool active)
{
    m_audioActive = active;
    if (!m_audioIndicator || !m_audioTimer)
        return;

    if (active)
    {
        if (!m_audioTimer->isActive())
            m_audioTimer->start();
        tickTitleAudioIndicator();
        return;
    }

    m_audioTimer->stop();
    m_audioPhase = 0;
    m_audioIndicator->clear();
}

void AchromaWindow::tickTitleAudioIndicator()
{
    if (!m_audioIndicator || !m_audioActive)
        return;

    static const QStringList frames = {"▁▃▆▃▁", "▂▄▇▄▂", "▃▆█▆▃", "▂▄▇▄▂"};
    m_audioIndicator->setText(frames.at(m_audioPhase % frames.size()));
    m_audioIndicator->setStyleSheet(
        "color: #6d6d6d; font-family: monospace; font-size: 10px; letter-spacing: 1px; border: none;"
    );
    ++m_audioPhase;
}

void AchromaWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_helpFrame)
        m_helpFrame->resize(m_central->size());
    if (m_finder && m_finder->isVisible())
        m_finder->reposition();
}

void AchromaWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings("Achroma", "Achroma");
    int tabs = m_browser ? m_browser->tabCount() : 0;
    bool skipConfirm = settings.value("skipCloseConfirm", false).toBool();
    if (tabs > 1 && !skipConfirm)
    {
        QDialog dialog(this);
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setWindowTitle("Achroma");
        dialog.setModal(true);
        dialog.setFixedWidth(360);
        dialog.setStyleSheet(
            "QDialog { background: #060606; border: 1px solid #1f1f1f; }"
            "QLabel { color: #d0d0d0; font-family: monospace; border: none; }"
            "QCheckBox { color: #5f5f5f; font-family: monospace; font-size: 10px; spacing: 7px; }"
            "QCheckBox::indicator { width: 10px; height: 10px; border: 1px solid #303030; background: #0b0b0b; }"
            "QCheckBox::indicator:checked { background: #cfcfcf; border-color: #cfcfcf; }"
            "QPushButton { background: transparent; color: #777; border: 1px solid #252525; border-radius: 3px; "
            "padding: 5px 14px; min-width: 72px; font-family: monospace; font-size: 11px; }"
            "QPushButton:hover { background: #101010; border-color: #3a3a3a; color: #ddd; }"
            "QPushButton#closeButton { color: #cfcfcf; border-color: #343434; }"
            "QPushButton#closeButton:hover { background: #161616; border-color: #555; color: #fff; }"
            "QToolButton { background: #141414; border: none; border-radius: 4px; }"
        );

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(0, 0, 0, 16);
        layout->setSpacing(0);

        auto* chrome = new QWidget(&dialog);
        chrome->setFixedHeight(26);
        chrome->setStyleSheet("QWidget { background: #050505; border-bottom: 1px solid #111; }");
        auto* chromeLayout = new QHBoxLayout(chrome);
        chromeLayout->setContentsMargins(13, 0, 13, 0);
        chromeLayout->setSpacing(0);
        auto* chromeTitle = new QLabel("close", chrome);
        chromeTitle->setAlignment(Qt::AlignCenter);
        chromeTitle->setStyleSheet("color:#343434;font-family:monospace;font-size:9px;letter-spacing:2px;border:none;");
        chromeLayout->addWidget(chromeTitle);
        layout->addWidget(chrome);

        auto* body = new QWidget(&dialog);
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(22, 18, 22, 0);
        bodyLayout->setSpacing(10);

        auto* title = new QLabel("Close Achroma?", body);
        title->setStyleSheet("color:#eeeeee;font-size:14px;font-weight:bold;font-family:monospace;border:none;");
        bodyLayout->addWidget(title);

        auto* message = new QLabel(QString("%1 tabs open. Session will be saved.").arg(tabs), body);
        message->setWordWrap(true);
        message->setStyleSheet("color:#7e7e7e;font-size:11px;font-family:monospace;border:none;");
        bodyLayout->addWidget(message);

        QCheckBox* cb = new QCheckBox("Don't ask again", body);
        bodyLayout->addWidget(cb);

        auto* buttons = new QHBoxLayout();
        buttons->setContentsMargins(0, 4, 0, 0);
        buttons->addStretch();
        auto* cancelBtn = new QPushButton("Cancel", body);
        auto* closeBtn = new QPushButton("Close", body);
        closeBtn->setObjectName("closeButton");
        buttons->addWidget(cancelBtn);
        buttons->addWidget(closeBtn);
        bodyLayout->addLayout(buttons);
        layout->addWidget(body);

        connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

        if (dialog.exec() != QDialog::Accepted)
        {
            event->ignore();
            return;
        }
        if (cb->isChecked())
            settings.setValue("skipCloseConfirm", true);
    }
    if (m_browser)
        m_browser->saveSession();
    QMainWindow::closeEvent(event);
}

bool AchromaWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (m_titleBar && (obj == m_titleBar || m_titleBar->isAncestorOf(qobject_cast<QWidget*>(obj))))
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
            {
                m_dragging = true;
                m_dragPos = me->globalPosition().toPoint() - frameGeometry().topLeft();
                return false;
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_dragging && (me->buttons() & Qt::LeftButton) && !isMaximized())
            {
                move(me->globalPosition().toPoint() - m_dragPos);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            m_dragging = false;
        }
        else if (event->type() == QEvent::MouseButtonDblClick)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
                isMaximized() ? showNormal() : showMaximized();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* k = static_cast<QKeyEvent*>(event);

        if (k->key() == Qt::Key_F12)
        {
            openDevTools();
            return true;
        }

        QWidget* w = qobject_cast<QWidget*>(obj);
        QTermWidget* tw = m_terminal->widget();
        if (w && (w == tw || tw->isAncestorOf(w)))
        {
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
            {
                if (m_cmdBuffer.startsWith(':'))
                {
                    m_terminal->sendText("\x15");
                    m_terminal->sendText("# " + m_cmdBuffer + "\n");
                    m_dispatcher->execute(m_cmdBuffer.mid(1));
                    m_cmdBuffer.clear();
                    return true;
                }
                m_cmdBuffer.clear();
            }
            else if (k->key() == Qt::Key_Escape && m_cmdBuffer.startsWith(':'))
            {
                m_terminal->sendText("\x15");
                m_cmdBuffer.clear();
                return true;
            }
            else if (k->key() == Qt::Key_Backspace && !m_cmdBuffer.isEmpty())
            {
                m_cmdBuffer.chop(1);
                if (!m_cmdBuffer.isEmpty() && m_cmdBuffer.back().isLowSurrogate())
                    m_cmdBuffer.chop(1);
            }
            else
            {
                QString text = k->text();
                if (!text.isEmpty() && text[0].isPrint())
                {
                    if (m_cmdBuffer.isEmpty() && text == ":")
                        m_cmdBuffer = ":";
                    else if (!m_cmdBuffer.isEmpty())
                        m_cmdBuffer += text;
                    if (m_cmdBuffer.size() > 1024)
                        m_cmdBuffer.clear();
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void AchromaWindow::openDevTools()
{
    QWebEngineView* v = m_browser ? m_browser->currentView() : nullptr;
    if (!v)
        return;

    if (m_devToolsBar->isVisible())
    {
        disconnect(m_devToolsTargetConn);
        if (m_devToolsTarget)
            m_devToolsTarget->setDevToolsPage(nullptr);
        m_devToolsTarget = nullptr;
        QList<int> sizes = m_splitter->sizes();
        if (sizes.size() >= 3)
        {
            sizes[0] += sizes[1];
            sizes[1] = 0;
            m_splitter->setSizes(sizes);
        }
        m_devToolsBar->hide();
        return;
    }

    attachDevToolsTo(v);
    m_devToolsBar->show();

    QList<int> sizes = m_splitter->sizes();
    if (sizes.size() >= 3 && sizes[1] == 0)
    {
        int devH = qMax(200, sizes[0] / 2);
        sizes[0] = qMax(100, sizes[0] - devH);
        sizes[1] = devH;
        m_splitter->setSizes(sizes);
    }
}

void AchromaWindow::attachDevToolsTo(QWebEngineView* v)
{
    disconnect(m_devToolsTargetConn);
    if (m_devToolsTarget)
        m_devToolsTarget->setDevToolsPage(nullptr);
    m_devToolsTarget = v->page();
    m_devToolsTarget->setDevToolsPage(m_devToolsView->page());

    m_devToolsTargetConn = connect(
        m_devToolsTarget,
        &QObject::destroyed,
        this,
        [this]()
        {
            m_devToolsTarget = nullptr;
            m_devToolsBar->hide();
        }
    );
    v->triggerPageAction(QWebEnginePage::InspectElement);
}

void AchromaWindow::setStatus(const QString& msg, int timeoutMs)
{
    m_statusLabel->setText(msg);
    if (timeoutMs <= 0)
        return;
    QPointer<QLabel> guard(m_statusLabel);
    QString text = msg;
    QTimer::singleShot(
        timeoutMs,
        m_statusLabel,
        [guard, text]()
        {
            if (guard && guard->text() == text)
                guard->setText(QString());
        }
    );
}
