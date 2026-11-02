# PRISMA P-1 (v0.2) – instrukcja

Prisma to **instrument** do Abletona (ścieżka MIDI). Wrzucasz do niej zestaw brzmień, a każde uderzenie klawisza gra **inne brzmienie**: losowo, po kolei albo według zapisanego wzoru. Wysokość dźwięku nie zmienia się z klawiszem.

## 1. Wrzucanie brzmień (czarny ekran)
- **Kilka plików naraz** (przeciągnij z Findera lub przeglądarki Abletona albo kliknij LOAD i zaznacz kilka): każdy plik staje się osobnym brzmieniem. Wtyczka sama **ucina ciszę na początku** (każde brzmienie startuje od razu na uderzeniu), wyrównuje głośność i dodaje mikro-wyciszenie na końcu. Kolejność jest alfabetyczna według nazw plików.
- **Jeden plik**: zostaje pocięty na części. **SPLIT** = *Equal* (równe części, liczba w **PARTS**) albo *Transients* (cięcie na uderzeniach).
- Prawy klik na brzmieniu na ekranie (przy kilku plikach) = usuń je. **CLEAR** czyści wszystko.
- Maksymalnie 32 brzmienia.

## 2. Zakładka PLAY
- **RATE** (duża gałka): jak często brzmienie się powtarza, gdy **trzymasz klawisz** (1/1 … 1/64, T = triole, D = kropkowane). **OFF** = jedno uderzenie na nutę, jak zwykły sampler.
- **LENGTH**: jak długo gra każde powtórzenie (krótko = staccato i stutter).
- **SWING**: opóźnia co drugie powtórzenie.
- **DECAY**: kolejne powtórzenia coraz ciszej, jak echo.
- **MODE**: *One-Shot* (gra do końca), *Gate* (gra, dopóki trzymasz), *Loop* (zapętla), *Ping-Pong* (zapętla tam i z powrotem).
- **GRID SYNC**: włączone = powtórzenia trzymają siatkę taktu Abletona; wyłączone = liczą się od momentu wciśnięcia klawisza.
- **CHOKE**: nowa nuta ucina poprzednią.

## 3. Zakładka ORDER (które brzmienie gra przy kolejnym uderzeniu)
- **Random**: losowo. **No Repeat**: losowo, ale nigdy dwa razy to samo pod rząd. **Cycle**, **Backward**, **Ping-Pong**: po kolei.
- **Keys**: C1 = brzmienie 1, C#1 = brzmienie 2 itd. **Rand Keys**: brzmienia losowo rozrzucone po klawiszach (**SHUFFLE KEYS** tasuje od nowa).
- **CAPTURE**: zapamiętuje kolejność, którą właśnie słyszałeś, i włącza **LOCK**. Od tej chwili ten przebieg gra zawsze tak samo.
- **LOCK**: gra zapisany wzór (widoczny na dole ekranu). Wyłącz LOCK, żeby wrócić do losowania.
- **Edycja wzoru**: kliknij kratkę na dole ekranu, żeby zmienić jej brzmienie (lewy klik = następne, prawy = poprzednie).
- **DICE PATTERN**: nowy losowy wzór (od razu zablokowany).
- **STEPS**: długość wzoru (4, 8, 16, 32 uderzenia). **RESET**: od początku wzoru przy starcie odtwarzania (zawsze) i dodatkowo co 1, 2 albo 4 takty.
- **REVERSE**: szansa, że uderzenie zagra od tyłu.

## 4. Sekcje po prawej
- **AMP**: Attack, Decay, Sustain, Release całej nuty.
- **TONE**: Low Cut (wycina bas, przydaje się przy cięciu całych utworów), High Cut, Reso.
- **COLOR**: Crunch (przester), Wash (zamglenie, pogłos, trzaski), Pump (pompowanie w ćwierćnutach).
- **MASTER**: Tune (półtony), Fine (centy), Volume.

## Szybki przepis na efekt z filmu z FL
1. Przeciągnij kilka brzmień tej samej nuty (albo jeden fragment utworu i ustaw PARTS).
2. RATE 1/16, MODE One-Shot, ORDER Random.
3. Trzymaj jeden klawisz albo wpisz w Abletonie długą nutę.
4. Gdy trafi się fajny przebieg: **CAPTURE**. Gotowe, gra powtarzalnie.

## Przekazywanie zmian
- **COPY** kopiuje wszystkie ustawienia (razem ze wzorem) jako tekst. Wklej go do czatu i opisz, co zmienić.
- **PASTE** wkleja ustawienia, które Ci wyślę.
- Numer wersji jest obok logo. Uwagi dziel na: błędy, brzmienie, wygląd.

## Instalacja (Mac)
```
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
cp -R ~/Downloads/Prisma-macOS/Prisma.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R ~/Downloads/Prisma-macOS/Prisma.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Prisma.vst3 ~/Library/Audio/Plug-Ins/Components/Prisma.component
```
Jeśli masz już starą wersję, te komendy ją nadpiszą. Potem w Abletonie: Settings → Plug-Ins → Option + **Rescan**. Wtyczka zapamiętuje ścieżki do plików, więc nie przenoś ich po wczytaniu.
