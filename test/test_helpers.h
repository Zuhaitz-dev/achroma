#pragma once
#include <QtTest>

class TestUtils : public QObject
{
    Q_OBJECT

private slots:
    void testFormatUrl_data();
    void testFormatUrl();

    void testSearchUrl_data();
    void testSearchUrl();

    void testStripTerminalControls();
    void testStripCarriageReturn();
    void testUrlEncode();
    void testDuckDuckGoUrl();
    void testBuiltinSearchEnginesExist();

    void testErrorPattern();
    void testFatalErrorPattern();
    void testUrlPattern();
    void testLinkerErrorPattern();
    void testMultipleUrlsInOutput();
    void testLongUrlExtraction();

    void testUtf8RoundTrip();
    void testStripComplexAnsi();
    void testStripOscSequences();
    void testPatternDebounceConcept();
    void testUrlBarSearchPrefix();
    void testFormatUrlEdgeCases();
    void testUrlEncodeEdgeCases();

    void testKeyScrollScript();
    void testLinkHintsScript();
    void testCodeBlockScript();
    void testInstallCmdScript();
    void testHomePageHtml();

    void testInstallCmdNpmMatch();
    void testInstallCmdPipMatch();
    void testInstallCmdCargoMatch();
    void testInstallCmdAptMatch();
    void testInstallCmdGitCloneMatch();
    void testInstallCmdNoFalsePositives();
};
