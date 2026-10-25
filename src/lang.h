// Everything the UI says, in English or Turkish. The setting picks one (by default the Windows display language);
// DESKFIND_LANG=en|tr overrides both.
#pragma once
#include "common.h"

struct Lang
{
    const wchar_t* locale; // for DirectWrite and dates
    wchar_t thousands;     // digit group separator in counts

    // Search box and list.
    const wchar_t* placeholder;
    const wchar_t* idleHint;
    const wchar_t* desktop; // group title and location
    const wchar_t* publicDesktop;
    const wchar_t* desktopFolders;
    const wchar_t* elsewhere;
    const wchar_t* recent;
    const wchar_t* folder;
    const wchar_t* shortcut;

    // Footer.
    const wchar_t* result; // after "1"
    const wchar_t* results;
    const wchar_t* noResults;
    const wchar_t* searching;
    const wchar_t* hintOpen;
    const wchar_t* hintReveal;
    const wchar_t* hintClose;
    const wchar_t* pathCopied;
    const wchar_t* openFailed; // followed by the reason
    const wchar_t* revealFailed;
    const wchar_t* unknownError;
    const wchar_t* itemNotFound;
    const wchar_t* explorerFailed;
    const wchar_t* everythingLoading;
    const wchar_t* everythingStarting;
    const wchar_t* everythingMissing;
    const wchar_t* everythingNoStart;
    const wchar_t* everythingNoOpen;

    // Tray and setup.
    const wchar_t* trayTip; // "%s": the desktop shortcut
    const wchar_t* menuSearch;
    const wchar_t* menuSettings;
    const wchar_t* menuAutostart;
    const wchar_t* menuAbout;
    const wchar_t* menuExit;
    const wchar_t* welcomeTitle;
    const wchar_t* welcomeText;
    const wchar_t* uninstalled;

    // Settings window.
    const wchar_t* settingsTitle;
    const wchar_t* navGeneral;
    const wchar_t* navShortcuts;
    const wchar_t* navSearch;
    const wchar_t* navAppearance;
    const wchar_t* navStats;
    const wchar_t* navAbout;
    const wchar_t* on;
    const wchar_t* off;
    const wchar_t* keySpace;

    const wchar_t* secStartup;
    const wchar_t* autostartTitle;
    const wchar_t* autostartDesc;
    const wchar_t* trayTitle;
    const wchar_t* trayDesc;
    const wchar_t* secLanguage;
    const wchar_t* languageTitle;
    const wchar_t* languageDesc;
    const wchar_t* languageSystem;
    const wchar_t* secReset;
    const wchar_t* resetTitle;
    const wchar_t* resetDesc;
    const wchar_t* resetButton;

    const wchar_t* secOpen;
    const wchar_t* desktopKeyTitle;
    const wchar_t* desktopKeyDesc;
    const wchar_t* globalKeyTitle;
    const wchar_t* globalKeyDesc;
    const wchar_t* keyRecording;
    const wchar_t* keyTaken;
    const wchar_t* keyNeedsModifier;
    const wchar_t* keyNone;
    const wchar_t* keyChange;
    const wchar_t* secInBox;
    const wchar_t* inBoxCopy;
    const wchar_t* inBoxEverything;
    const wchar_t* inBoxAgain;
    const wchar_t* inBoxSettings;
    const wchar_t* inBoxMove;

    const wchar_t* secGroups;
    const wchar_t* groupsTitle;
    const wchar_t* groupsDesc;
    const wchar_t* limitFormat; // "%u results"
    const wchar_t* moveUp;
    const wchar_t* moveDown;
    const wchar_t* secWhat;
    const wchar_t* hiddenTitle;
    const wchar_t* hiddenDesc;
    const wchar_t* hiddenExplorer;
    const wchar_t* hiddenShow;
    const wchar_t* hiddenHide;
    const wchar_t* devTitle;
    const wchar_t* devDesc;
    const wchar_t* systemTitle;
    const wchar_t* systemDesc;
    const wchar_t* extraTitle;
    const wchar_t* extraDesc;
    const wchar_t* excludedTitle;
    const wchar_t* excludedDesc;
    const wchar_t* addFolder;
    const wchar_t* removeFolder;
    const wchar_t* secRanking;
    const wchar_t* boostTitle;
    const wchar_t* boostDesc;
    const wchar_t* recentTitle;
    const wchar_t* recentDesc;

    const wchar_t* secWindow;
    const wchar_t* themeTitle;
    const wchar_t* themeDesc;
    const wchar_t* themeSystem;
    const wchar_t* themeLight;
    const wchar_t* themeDark;
    const wchar_t* widthTitle;
    const wchar_t* widthDesc;
    const wchar_t* widthNarrow;
    const wchar_t* widthNormal;
    const wchar_t* widthWide;
    const wchar_t* monitorTitle;
    const wchar_t* monitorDesc;
    const wchar_t* monitorMouse;
    const wchar_t* monitorMain;
    const wchar_t* secEffects;
    const wchar_t* transparencyTitle;
    const wchar_t* transparencyDesc;
    const wchar_t* animationsTitle;
    const wchar_t* animationsDesc;
    const wchar_t* hintsTitle;
    const wchar_t* hintsDesc;

    const wchar_t* secUsage;
    const wchar_t* statOpens;
    const wchar_t* statSearches;
    const wchar_t* statLaunches;
    const wchar_t* statAverage;
    const wchar_t* statToday;
    const wchar_t* statRemembered;
    const wchar_t* statSince; // "%s": a date
    const wchar_t* secTop;
    const wchar_t* topEmpty;
    const wchar_t* timesOne;
    const wchar_t* timesFormat; // "%u times"
    const wchar_t* secEverything;
    const wchar_t* everythingState;
    const wchar_t* everythingRunning; // "%s": the version
    const wchar_t* everythingRunningPlain;
    const wchar_t* everythingStopped;
    const wchar_t* indexTitle;
    const wchar_t* indexReady; // "%s entries · %s"
    const wchar_t* indexIdle;
    const wchar_t* secData;
    const wchar_t* clearHistoryTitle;
    const wchar_t* clearHistoryDesc;
    const wchar_t* clearButton;
    const wchar_t* resetStatsTitle;
    const wchar_t* resetStatsDesc;
    const wchar_t* openDataTitle;
    const wchar_t* openDataDesc;

    const wchar_t* aboutTagline;
    const wchar_t* versionFormat; // "%s"
    const wchar_t* secLinks;
    const wchar_t* linkGitHub;
    const wchar_t* linkIssues;
    const wchar_t* linkLicense;
    const wchar_t* linkEverything;
    const wchar_t* aboutNote;

    static const Lang& Get();
    static void Set(int choice); // 0 Windows display language, 1 English, 2 Turkish
};
