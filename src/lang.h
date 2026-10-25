// Everything the UI says, in English or Turkish. The Windows display language picks one; DESKFIND_LANG=en|tr overrides it.
#pragma once
#include "common.h"

struct Lang
{
    const wchar_t* locale; // for DirectWrite
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
    const wchar_t* trayTip;
    const wchar_t* menuSearch;
    const wchar_t* menuAutostart;
    const wchar_t* menuAbout;
    const wchar_t* menuExit;
    const wchar_t* welcomeTitle;
    const wchar_t* welcomeText;
    const wchar_t* uninstalled;

    static const Lang& Get();
};
