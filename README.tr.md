<p align="center">
  <img src="docs/icon.png" width="80" height="80" alt="">
</p>

<h1 align="center">DeskFind</h1>

<p align="center">
  <b>Windows masaüstü için Ctrl+F.</b><br>
  Masaüstündeyken Ctrl+F'ye bas ve yaz: önce masaüstü, sonra üzerindeki klasörler, sonra bilgisayarın geri kalanı.
</p>

<p align="center">
  <a href="https://github.com/Bilalatss/DeskFind/releases/latest"><img src="https://img.shields.io/github/v/release/Bilalatss/DeskFind?label=s%C3%BCr%C3%BCm" alt="Son sürüm"></a>
  <img src="https://img.shields.io/badge/Windows-11-0078D4" alt="Windows 11">
  <img src="https://img.shields.io/badge/boyut-460%20KB-2ea44f" alt="460 KB">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/Bilalatss/DeskFind?label=lisans" alt="MIT lisansı"></a>
</p>

<p align="center">
  <img src="docs/screenshot.png" width="680" alt="Masaüstünde açık DeskFind; sonuçlar Masaüstü, Masaüstündeki klasörler ve Bilgisayarın geri kalanı olarak gruplanmış">
</p>

<p align="center"><a href="README.md">English</a></p>

---

Windows'ta yalnızca masaüstünü hızlıca aramanın bir yolu yok. Başlat araması tüm bilgisayara bakar; uygulamaları ve
web sonuçlarını da araya katar. Gezgin araması ise önce bir pencere açmayı gerektirir ve yavaştır.

DeskFind, masaüstünün kendi Ctrl+F'sine bir arama kutusu koyar, ama yalnızca masaüstünde. Diğer her pencerede Ctrl+F o
uygulamanın kendi kısayolu olarak kalır. Her yerde ya da başka tuşlarla açılmasını istersen, bu bir ayar uzaklığında.

## Özellikler

- **Önce masaüstü.** Sonuçlar üç grupta gelir: *Masaüstü*, *Masaüstündeki klasörler* ve *Bilgisayarın geri kalanı*.
- **Anında.** Masaüstü ve klasörleri bellekten, her tuşta 1–3 ms'de gelir. Bilgisayarın geri kalanı [Everything](https://www.voidtools.com) ile 40–60 ms civarında gelir.
- **Simgeyi bulur.** Ctrl+Enter, dosyanın simgesini masaüstünde seçer. Dosya başka bir yerdeyse klasörünü Gezgin'de açar.
- **Öğrenir.** Sık açtıkların üste çıkar. Kutu boşken son açtıkların listelenir.
- **Gürültüyü ayıklar.**
  - `node_modules`, `.git`, `__pycache__` ve benzeri klasörler hiç görünmez.
  - Windows klasörü, geçici dosyalar ve Windows'un *Son kullanılanlar* kısayolları da görünmez.
  - Derleme çıktıları, uygulama verileri ve loglar senin dosyalarından sonra gelir.
- **Sana göre ayarlanır.** Windows 11 Ayarlar tarzında bir ayarlar penceresi var. Kısayollar, sonuç grupları, hangi klasörlerin arandığı, görünüm ve daha fazlası oradan değişir. Her değişiklik anında uygulanır.
- **Küçük.** 460 KB'lık tek bir exe, ayrıca kurulacak bir çalışma ortamı yok. Tepside beklerken 1 MB'tan az bellek kullanır.
- **Windows'un parçası gibi görünür.** Akrilik, Mica, yuvarlak köşeler, açık/koyu tema ve vurgu rengi tıpkı Windows'taki gibi.
- **Erişilebilir.** Ekran Okuyucusu ve diğer ekran okuyucular arama kutusunu, sonuçları ve tüm ayarları okuyup
  kullanabilir. Windows'un karşıtlık temalarına da uyar.
- **Türkçe ve İngilizce.** Sen seçmezsen Windows'un görüntüleme dilini izler.
- **Gizlilik.** İnternete bağlanmaz, veri toplamaz.

## Kurulum

Windows 11 gerekir. Masaüstünün dışında da aramak için [Everything](https://www.voidtools.com/downloads/) (ücretsiz)
lazım; betik onu kurmayı önerir. PowerShell'e şunu yapıştır:

```powershell
irm https://raw.githubusercontent.com/Bilalatss/DeskFind/main/install.ps1 | iex
```

[Betik](install.ps1) şunları yapar:

- Son sürümdeki `DeskFind.exe`'yi (Arm işlemcili Windows'ta Arm64 sürümünü) indirir ve SHA-256 özetini sürümdekiyle karşılaştırır.
- Everything kurulu değilse winget ile kurmayı önerir.
- Dosyayı `%LOCALAPPDATA%\Programs\DeskFind` klasörüne kopyalar.
- DeskFind'ı Başlat menüsüne ve *Ayarlar › Uygulamalar*'a ekler.
- DeskFind'ı başlatır.

Bundan sonra DeskFind Windows ile birlikte açılır; bunu tepsi menüsünden kapatabilirsin. Betik yönetici yetkisi
istemez (Everything'in kurulumu bir kez ister). Güncellemek için aynı satırı tekrar çalıştırman yeterli.

<details>
<summary>Neden setup.exe ya da .msi değil de komut?</summary>

DeskFind henüz kod imzalı değil. Tarayıcıdan indirilen imzasız bir `.exe` ya da `.msi` genellikle SmartScreen
uyarısı alır; PowerShell'in indirdiği dosya almaz. Betik kısa; çalıştırmadan önce okuyabilirsin.
</details>

<details>
<summary>Elle kurulum</summary>

[Releases](https://github.com/Bilalatss/DeskFind/releases/latest) sayfasından `DeskFind.exe`'yi indir. O klasörde
PowerShell'i aç ve `.\DeskFind.exe --install` komutunu çalıştır. Dosya tarayıcıdan geldiği için Windows önce uyarı
gösterebilir.

Exe'yi kurmadan, herhangi bir klasörden de çalıştırabilirsin. Bu durumda Başlat menüsünde ve *Ayarlar ›
Uygulamalar*'da görünmez, ama Windows açılışında o klasörden başlaması yine de açılır.
</details>

**Kaldırma:** *Ayarlar › Uygulamalar › DeskFind › Kaldır* yolunu kullan ya da şu komutu çalıştır:

```powershell
& "$env:LOCALAPPDATA\Programs\DeskFind\DeskFind.exe" --uninstall
```

Bu işlem exe'yi, Başlat menüsü kaydını, ayarlarını, geçmişini ve otomatik başlatma kaydını siler.

## Kullanım

Masaüstünde boş bir yere tıkla (ya da <kbd>Win</kbd>+<kbd>D</kbd>), <kbd>Ctrl</kbd>+<kbd>F</kbd>'ye bas ve yaz.

| Tuş | İşlev |
| --- | --- |
| <kbd>↑</kbd> <kbd>↓</kbd> · <kbd>PgUp</kbd> <kbd>PgDn</kbd> | Sonuçlarda gezin |
| <kbd>Enter</kbd> ya da tıklama | Aç |
| <kbd>Ctrl</kbd>+<kbd>Enter</kbd> ya da <kbd>Ctrl</kbd>+tıklama | Göster: simgesini masaüstünde seç ya da klasörünü aç |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> | Tam yolu kopyala |
| <kbd>Ctrl</kbd>+<kbd>E</kbd> | Aramaya Everything'de devam et |
| <kbd>F5</kbd> | Yeniden ara |
| <kbd>Ctrl</kbd>+<kbd>,</kbd> ya da ⚙ düğmesi | Ayarları aç |
| <kbd>Esc</kbd> | Kapat ve masaüstüne dön |

**Arama ipuçları**

- Kelimeler herhangi bir sırayla eşleşir; büyük/küçük harf ve aksanlar önemli değildir: `cafe` *Café*'yi, `i` *ı*'yı bulur.
- Everything'in sözdizimi de çalışır: `ext:pdf`, `*.psd`, `dm:today`, `!taslak`, `fatura | makbuz`.
- Kutuyu tepsi simgesine (çoğunlukla <kbd>^</kbd> altında gizlidir) ya da Başlat menüsündeki *DeskFind*'a tıklayarak da açabilirsin.
- Tepsi simgesine sağ tıkladığında *Ayarlar*, *Windows ile başlat*, *DeskFind hakkında* ve *Çıkış* seçenekleri gelir.

## Ayarlar

Ayarları tepsi menüsünden, arama kutusundaki ⚙ düğmesinden, <kbd>Ctrl</kbd>+<kbd>,</kbd> ile ya da `DeskFind.exe --settings`
komutuyla açabilirsin.

<p align="center">
  <img src="docs/settings.png" width="680" alt="DeskFind ayarlar penceresinin Arama sayfası: üç sonuç grubunun sırası, boyutu ve anahtarları, ardından neyin arandığı">
</p>

| Sayfa | Neler değişir |
| --- | --- |
| **Genel** | Windows ile başlatma, tepsi simgesi, dil, varsayılanlara dönüş |
| **Kısayollar** | Masaüstü kısayolu ve DeskFind'ı her pencerenin üstünde açan ikinci bir kısayol (başta kapalı, <kbd>Win</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>). Kısayola tıklayıp yeni tuşlara basman yeterli; tuşları başka bir uygulama kullanıyorsa DeskFind söyler. |
| **Arama** | Üç grubun sırası, her birinin kaç sonuç gösterdiği ve hangilerinin açık olduğu. Gizli dosyalar, geliştirici ve sistem klasörleri, masaüstü gibi aranan ek klasörler (örneğin İndirilenler), hariç tutulan klasörler ve sıralama. Everything'in durumu ve onu kuran ya da başlatan bir düğme. |
| **Görünüm** | Tema, arama kutusunun genişliği, hangi ekranda açıldığı, saydamlık, animasyonlar, tuş ipuçları |
| **İstatistikler** | Aramalar, açılan öğeler, ortalama cevap süresi, en çok açtıkların, klasör dizininin boyutu. Geçmişi temizleme ve sayaçları sıfırlama. |
| **Hakkında** | Sürüm, bağlantılar ve lisans |

## Nasıl çalışır

| Grup | Kaynak |
| --- | --- |
| **Masaüstü** | Kutu her açıldığında doğrudan diskten listelenir, bu yüzden hep günceldir. Gezgin'in gizli dosya ayarlarına uyar ve Ortak masaüstünü de kapsar. |
| **Masaüstündeki klasörler** | Kutu açılınca tek bir Everything sorgusu, masaüstünün altındaki tüm dosya ve klasörleri bellekteki küçük bir dizine yükler (26.000 kayıt 1,4 MB tutar). Everything yoksa aynı ağaç arka planda diskten okunur (26.000 kayıt yaklaşık 0,3 sn). Her tuşta bu dizin yerelde süzülür. Dizin, kutu kapandıktan birkaç saniye sonra bellekten atılır. |
| **Bilgisayarın geri kalanı** | Everything'in IPC arayüzüyle çalışır; SDK DLL'i gerekmez. Everything en son değişen 1.000 eşleşmeyi gönderir, DeskFind bunları sıralar. Cevaplar önbelleğe alınır. Kısa bir sorgu tüm eşleşmeleri getirdiyse, onu uzatan sorgu yerelde süzülür. |

- **Sıralama:**
  - Önce adın eşleşme biçimine bakılır: tam ad, adın başı, bir kelimenin başı, adın herhangi bir yeri.
  - Eşleşmesi aynı olanlarda sık ve yakın zamanda açtıkların yukarı çıkar.
  - Derleme çıktıları, önbellekler ve uygulama verileri aşağı iner.
- **Çizim:** DeskFind, ayarlar dahil pencerelerini kendisi çizer. DirectWrite ile hazırlanan yazılar işlemcide bir bitmap'e çizilir ve DWM akrilik penceresinde gösterilir. Arayüz çatısı ya da GPU bağlamı yoktur; bu yüzden gizliyken bellekte tutacak pek bir şeyi yoktur.
- **Erişilebilirlik:** Ekrandaki hiçbir şey standart bir denetim olmadığı için iki pencere de kendini ekran okuyuculara UI Automation ile tarif eder. Karşıtlık temasında saydamlığı bırakır ve temanın kendi renkleriyle çizer.
- **Boşta:** Kutu kapandıktan birkaç saniye sonra önbelleklerini bırakır, belleğini küçültür ve Windows'tan verimlilik modunda çalışmayı ister.
- **Hız:** Kutu açıkken DeskFind, Everything'i Windows 11'in verimlilik modundan çıkarır. Testlerde Everything'in sorguları yaklaşık %30 hızlandı.

## Performans

Geliştiricinin bilgisayarında ölçüldü (Windows 11 24H2, Everything'de yaklaşık 2,6 milyon dosya):

| | |
| --- | --- |
| Ctrl+F'den pencerenin ekranda olmasına | 10–33 ms |
| Masaüstü ve masaüstündeki klasörler, her tuşta | 1–3 ms |
| Bilgisayarın geri kalanı, Everything ile | 40–60 ms |
| Yazmaya devam ederken (önbellekten süzerek) | ~1 ms |
| Tepside beklerken bellek | 0,3–0,7 MB |
| Kutu açıkken bellek | ~5 MB |
| İndirme boyutu | 460 KB |

## Gereksinimler

- **Windows 11**, x64 ya da Arm64.
  - 24H2'de (x64) derlendi ve test edildi. Arm64 sürümü aynı CI'da derlenir ama bir Arm bilgisayarda test edilmedi.
  - Windows 10'da test edilmedi; pencere Windows 11'in akrilik arka planına ve yuvarlak köşelerine dayanıyor.
- **voidtools [Everything](https://www.voidtools.com/downloads/) 1.4**, normal sürüm; bilgisayarın geri kalanı için.
  - IPC'si olmayan *Everything Lite* çalışmaz.
  - Everything kurulu ama kapalıysa DeskFind onu başlatır.
  - 1.4.1 ile test edildi. 1.5 alfa sürümünün çalışması gerekir ama test edilmedi.
  - Everything olmadan da masaüstü ve klasörleri aranır. *Bilgisayarın geri kalanı* grubunda o zaman bir satır çıkar:
    Everything'i winget ile kurar (bir kez yönetici onayı ister) ya da indirme sayfasını açar.

## Kaynaktan derleme

Visual Studio 2022 veya sonrası (ya da Build Tools) ve *C++ ile masaüstü geliştirme* iş yükü gerekir.

```bat
build.cmd            :: bin\DeskFind.exe
build.cmd install    :: derle, sonra bu kullanıcıya kur
build.cmd test       :: derle, sonra testleri çalıştır (bin\DeskFindTests.exe)
build.cmd arm64      :: Arm işlemcili Windows için bin\DeskFind-arm64.exe (MSVC ARM64 derleme araçları gerekir)
build.cmd analyze    :: tüm kaynak dosyalarda MSVC kod analizi; tek bir uyarı bile derlemeyi durdurur
build.cmd trace      :: bin\DeskFindTrace.exe, süreleri %LOCALAPPDATA%\DeskFind\trace.log dosyasına yazar
```

Testler (`tests/tests.cpp`) ad eşleştirmeyi, Everything cevap ayrıştırıcısını bozuk cevaplara karşı ve tüm arama
hattını kendi kurduğu bir klasör ağacı üzerinde denetler. Ağaç diskten okunur, yani Everything gerekmez. GitHub Actions
kod analizini ve testleri her push ve pull request'te çalıştırır.

Arama hattının arayüzsüz bir testi de var. DeskFind pencereli bir program olduğu için `start /wait` ile beklenir:

```bat
start /wait bin\DeskFind.exe --selftest out.txt @index "d>de>des" rapor
```

Bu komutta:

- `>` önekleri tek tek yazar;
- `+` önekleri art arda hızlıca yazar;
- `@index` önce masaüstü klasörleri dizinini kurar;
- `@settings` kayıtlı ayarlara göre arar.

| Dosya | Ne yapar |
| --- | --- |
| `src/app.cpp` | Arama penceresinin yaşam döngüsü, tepsi, yalnızca masaüstünde çalışan kısayol ve Everything kurulumu |
| `src/appview.cpp`, `src/appinput.cpp` | Arama penceresinin gösterdikleri ve çizimi; metin kutusu, liste, klavye ve fare |
| `src/access.cpp`, `src/appaccess.cpp`, `src/settingsaccess.cpp` | Ekran okuyucular için UI Automation |
| `src/search.cpp` | Üç grup, sıralama, önbellek ve masaüstü klasörleri dizini |
| `src/everything.cpp` | Kendi iş parçacığında Everything IPC |
| `src/canvas.cpp`, `src/text.cpp` | Yazılım tabanlı çizim ve DirectWrite yazıları |
| `src/icons.cpp` | Arka planda yüklenen dosya simgeleri |
| `src/history.cpp` | Sıralama ve *Son açılanlar* için açtıkların |
| `src/tray.cpp`, `src/shell.cpp` | Tepsi simgesi ve menüsü; açma, konum gösterme ve otomatik başlatma |
| `src/settings.cpp`, `src/settingsui.cpp` | Ayarlar ve onları değiştiren pencere |
| `src/setup.cpp` | `--install` ve `--uninstall` |
| `src/lang.cpp` | Türkçe ve İngilizce metinler |
| `tests/tests.cpp` | Testler |

Sürümler `v*` etiketinden [GitHub Actions](.github/workflows/build.yml) ile derlenir. Etiket `src/version.h` ile
(ve `res/app.manifest` içindeki sürümle) aynı olmalıdır.

## Gizlilik

DeskFind internete bağlanmaz. Tepsi menüsündeki *DeskFind hakkında* bu sayfayı tarayıcında açar.

Diskte şunları tutar:

- Ondan açtığın öğelerin listesi (`history.txt`, en çok 300 kayıt). Sıralama ve *Son açılanlar* için kullanılır.
- Bir şey ters giderse bir `error.log`.

İkisi de `%LOCALAPPDATA%\DeskFind` klasöründedir. Kayıt defterinde, `HKCU\Software\DeskFind` altında, ayarlarını ve
kullanım sayaçlarını tutar. Sayaçlar yalnızca sayıdır; ad ya da arama içermez. Ayrıca otomatik başlatma kaydı ve
*Ayarlar › Uygulamalar* kaydı vardır. Kaldırınca hepsi silinir.

## Sık sorulanlar

**Ctrl+F açmıyor.** Masaüstünün etkin pencere olması gerekir; önce masaüstünde boş bir yere tıkla. Diğer pencerelerde Ctrl+F o pencereye aittir.

***Çıkış*'tan sonra nasıl yeniden açarım?** Başlat menüsünden *DeskFind*'ı aç.

**Türkçe Windows'ta İngilizce kullanabilir miyim, ya da tersi?** Evet: *Ayarlar › Genel › Dil*.

**Tepsi simgesini kapattım, ayarlara nasıl ulaşırım?** Arama kutusunu açıp ⚙ düğmesine tıkla (ya da <kbd>Ctrl</kbd>+<kbd>,</kbd>'e bas). DeskFind kapalıysa önce Başlat menüsünden aç.

**Akıllı Uygulama Denetimi engelliyor.** Akıllı Uygulama Denetimi imzasız uygulamaları engeller ve istisna tanımaz. DeskFind henüz imzalı değil.

**Hangi ekranda açılır?** Fare imlecinin bulunduğu ekranda.

**Everything'i kurmak zorunda mıyım?** Hayır. Masaüstü ve klasörleri onsuz da aranır; Everything'e yalnızca
*Bilgisayarın geri kalanı* grubu ihtiyaç duyar. O grupta çıkan satıra tıklarsan Everything kurulur. Satırı görmek
istemezsen grubu *Ayarlar › Arama*'dan kapat.

## Lisans

[MIT](LICENSE). DeskFind, voidtools'un [Everything](https://www.voidtools.com) programını herkese açık IPC arayüzü üzerinden kullanır; voidtools ile bir bağı yoktur.
