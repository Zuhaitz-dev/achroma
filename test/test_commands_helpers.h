#pragma once
#include <QtTest>

class TestCommands : public QObject
{
    Q_OBJECT

private slots:
    void testBuiltinsRegistered();
    void testSearchEngineDispatch();
    void testUnknownCommandNoCrash();
    void testAppearanceDefaults();
    void testKeyConfigDefaults();
    void testSearchEnginesDefault();
    void testDefaultTriggers();
    void testAdBlockEnabledDefault();
    void testSetAdBlockEnabled();
    void testNewBuiltins();
    void testHomepageDefaults();
    void testTriggerTemplate();
    void testContextAwareBuiltins();
    void testKeyConfigAdditions();
};
