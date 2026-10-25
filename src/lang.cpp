#include "lang.h"

namespace
{
constexpr Lang English{
    .locale = L"en-us",
    .thousands = L',',
    .placeholder = L"Search the desktop, then the whole PC",
    .idleHint = L"Desktop › its folders › the whole PC",
    .desktop = L"Desktop",
    .publicDesktop = L"Public desktop",
    .desktopFolders = L"In desktop folders",
    .elsewhere = L"Rest of this PC",
    .recent = L"Recently opened",
    .folder = L"Folder",
    .shortcut = L"Shortcut",
    .result = L"result",
    .results = L"results",
    .noResults = L"No results",
    .searching = L"Searching…",
    .hintOpen = L"Open",
    .hintReveal = L"Show location",
    .hintClose = L"Close",
    .pathCopied = L"Path copied",
    .openFailed = L"Couldn't open: ",
    .revealFailed = L"Couldn't show the location: ",
    .unknownError = L"unknown error",
    .itemNotFound = L"item not found",
    .explorerFailed = L"Explorer didn't open",
    .everythingLoading = L"Everything is loading its index…",
    .everythingStarting = L"Starting Everything…",
    .everythingMissing = L"Everything not found · install it from voidtools.com",
    .everythingNoStart = L"Everything didn't start · try starting it yourself",
    .everythingNoOpen = L"Couldn't open Everything",
    .trayTip = L"DeskFind · Ctrl+F on the desktop",
    .menuSearch = L"Search\tCtrl+F on the desktop",
    .menuAutostart = L"Start with Windows",
    .menuAbout = L"About DeskFind",
    .menuExit = L"Exit",
    .welcomeTitle = L"DeskFind is running",
    .welcomeText = L"Press Ctrl+F on the desktop to search. Its icon lives here in the tray.",
    .uninstalled = L"DeskFind has been removed.",
};

constexpr Lang Turkish{
    .locale = L"tr-tr",
    .thousands = L'.',
    .placeholder = L"Masaüstünde ara, sonra tüm bilgisayarda",
    .idleHint = L"Masaüstü › içindeki klasörler › tüm bilgisayar",
    .desktop = L"Masaüstü",
    .publicDesktop = L"Ortak masaüstü",
    .desktopFolders = L"Masaüstündeki klasörler",
    .elsewhere = L"Bilgisayarın geri kalanı",
    .recent = L"Son açılanlar",
    .folder = L"Klasör",
    .shortcut = L"Kısayol",
    .result = L"sonuç",
    .results = L"sonuç",
    .noResults = L"Sonuç yok",
    .searching = L"Aranıyor…",
    .hintOpen = L"Aç",
    .hintReveal = L"Konumu göster",
    .hintClose = L"Kapat",
    .pathCopied = L"Yol kopyalandı",
    .openFailed = L"Açılamadı: ",
    .revealFailed = L"Konum açılamadı: ",
    .unknownError = L"bilinmeyen hata",
    .itemNotFound = L"öğe bulunamadı",
    .explorerFailed = L"Gezgin açılamadı",
    .everythingLoading = L"Everything hazırlanıyor…",
    .everythingStarting = L"Everything başlatılıyor…",
    .everythingMissing = L"Everything bulunamadı · voidtools.com'dan kur",
    .everythingNoStart = L"Everything açılmadı · elle başlatmayı dene",
    .everythingNoOpen = L"Everything açılamadı",
    .trayTip = L"DeskFind · masaüstünde Ctrl+F",
    .menuSearch = L"Ara\tMasaüstünde Ctrl+F",
    .menuAutostart = L"Windows ile başlat",
    .menuAbout = L"DeskFind hakkında",
    .menuExit = L"Çıkış",
    .welcomeTitle = L"DeskFind çalışıyor",
    .welcomeText = L"Aramak için masaüstündeyken Ctrl+F'ye bas. Simgesi burada, sistem tepsisinde.",
    .uninstalled = L"DeskFind kaldırıldı.",
};
} // namespace

const Lang& Lang::Get()
{
    static const Lang& lang = []() -> const Lang&
    {
        wchar_t chosen[8] = {};
        if (GetEnvironmentVariableW(L"DESKFIND_LANG", chosen, ARRAYSIZE(chosen))) return EqualsNoCase(chosen, L"tr") ? Turkish : English;
        return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH ? Turkish : English;
    }();
    return lang;
}
