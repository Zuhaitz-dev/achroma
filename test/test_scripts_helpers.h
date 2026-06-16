#pragma once
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtTest>

class TestScripts : public QObject
{
    Q_OBJECT

private:
    bool waitForLoad(QWebEngineView* view, int timeoutMs = 5000);
    QVariant runSync(QWebEngineView* view, const QString& js, int timeoutMs = 5000);

private slots:
    void testKeyScrollInjectsGuard();
    void testKeyScrollDoesNotDoubleInject();
    void testLinkHintsCreatesOverlay();
    void testLinkHintsNoLinksReturns();
    void testCodeBlockExtractsPreCode();
    void testCodeBlockExtractsWithSelection();
    void testCodeBlockEmptyPage();
    void testHomePageHtmlLoadsWithoutErrors();
};
