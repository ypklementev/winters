# WinTers: разработка

Сборка, упаковка, публикация и устройство кода. Описание для пользователей: [README](../README.md).

## Платформы

- **Windows:** C++20 + Win32 + Direct3D 11 + DirectComposition. Один exe без зависимостей и без установщика; тот же exe упаковывается в MSIX для Microsoft Store.
- **macOS:** та же симуляция, оболочка на Objective-C++ (AppKit + Metal + CGWindowList) в `src/mac/`.

Симуляция (`src/sim`) и подготовка данных для GPU (`src/render/instances.*`, атлас, фигуры) общие для обеих платформ.

Версия задаётся в одном месте: `project(WinTers VERSION X.Y.Z)` в `CMakeLists.txt`.
CMake прокидывает её в ресурс VERSIONINFO (`src/app/version.h.in`), в манифест MSIX как `X.Y.Z.0` (Store требует 0 в четвёртой части), в `Info.plist` и в `version.txt`, по которому скрипты сборки называют файлы в `dist/`.

## Windows

### Portable exe

Нужны **Visual Studio 2022+** с workload «Desktop development with C++» (MSVC x64 и Windows 11 SDK; CMake входит в комплект).
В PowerShell в корне репозитория:

```powershell
.\build.ps1 -Target exe
```

Результат: `dist\WinTers_<версия>_x64.exe`. CRT слинкован статически, поэтому VC++ Redistributable не нужен.
Скрипт сам находит самый новый Windows SDK, в котором есть makeappx, makepri, signtool и C++/WinRT. Если такого SDK нет, он останавливается и ничего не скачивает.
Сборка лежит в `build\msvc`, тесты симуляции: `ctest --test-dir build\msvc -C Release --output-on-failure`.

Если PowerShell запрещает скрипты: `powershell -ExecutionPolicy Bypass -File .\build.ps1`.

### MSIX

```powershell
.\build.ps1 -Target msix              # только MSIX
.\build.ps1                           # exe + MSIX (-Target all)
.\build.ps1 -Target msix -TestSign    # + копия, подписанная тестовым сертификатом
```

- `dist\WinTers_<версия>_x64.msix` — неподписанный пакет. **Его загружают в Store**, Microsoft подписывает его сама.
- `dist\WinTers_<версия>_x64_testsigned.msix` и `dist\WinTers_TestCert.cer` (только с `-TestSign`) — для установки на своём компьютере.

Что делает скрипт: собирает layout в `build\msvc\msix\layout` (exe, `AppxManifest.xml`, `Assets`), создаёт `resources.pri` через makepri и упаковывает пакет через makeappx.
С `-TestSign` он один раз создаёт в `Cert:\CurrentUser\My` самоподписанный сертификат с Subject `CN=BEB569F2-AE9B-46F2-8C45-1FB02FB35E6E` (как Publisher в манифесте) и подписывает копию пакета через signtool.

Установка тестового пакета:

```powershell
# один раз, PowerShell от администратора: доверить тестовый сертификат
Import-Certificate -FilePath .\dist\WinTers_TestCert.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople
# обычный PowerShell
Add-AppxPackage -Path .\dist\WinTers_<версия>_x64_testsigned.msix
# удалить
Get-AppxPackage AfterHoursLabs.WinTers | Remove-AppxPackage
```

Файлы пакета:
- `packaging/AppxManifest.xml` — шаблон манифеста. Версию и `MaxVersionTested` (версия SDK, которым собирали) подставляет CMake в `build\msvc\msix\AppxManifest.xml`.
- `packaging/Assets/*.png` — логотипы. Генерируются из той же снежинки, что и иконка exe: `python3 tools/make_store_assets.py`.

### Portable и MSIX: один exe

Один и тот же `WinTers.exe` работает и как portable-программа, и внутри MSIX-пакета.
Режим определяется при запуске (`GetCurrentPackageFullName`, `src/app/package.cpp`). Разница только в автозапуске:

| | portable exe | MSIX (Store) |
|---|---|---|
| Автозапуск | ключ `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` | `StartupTask` с id `WinTersStartup` (C++/WinRT) |
| Настройки | `%APPDATA%\WinTers\settings.ini` | то же, Windows перенаправляет запись в папку пакета |
| Второй экземпляр | mutex `Local\WinTers.SingleInstance`, общий для обеих версий | |

MSIX при каждом запуске удаляет ключ `Run` от portable-версии, чтобы при входе в Windows не стартовали две копии.
Если в MSIX автозапуск выключен в «Параметры → Приложения → Автозагрузка», пункт меню становится неактивным и подсказывает, где его включить.

### Сборка без Windows (macOS / Linux)

**Кросс-сборка exe через mingw-w64:**

```sh
brew install mingw-w64            # macOS (~1.5 ГБ); на Linux: apt install mingw-w64
cmake -S . -B build/win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build/win
x86_64-w64-mingw32-strip build/win/WinTers.exe   # необязательно: ~2.5 МБ → ~0.4 МБ
```

Exe собирается статически, его можно сразу копировать на Windows.
В mingw нет C++/WinRT, поэтому такой exe годится только как portable: внутри MSIX автозапуск в нём будет недоступен. Пакеты для Store собирает только `build.ps1` (MSVC).

**Проверка HLSL-шейдеров без Windows.** HLSL компилируется на Windows при запуске. Синтаксис можно проверить заранее через glslang (`brew install glslang`).
Вырежьте текст между `R"hlsl(` и `)hlsl"` из `src/render/shaders.h` в `shaders.hlsl`, затем:

```sh
for e in vs_disc:vert vs_pile:vert ps_flake:frag ps_ball:frag ps_pile:frag; do
  glslangValidator -D -e ${e%%:*} -S ${e##*:} -V shaders.hlsl -o /dev/null; done
```

## macOS

Требования: macOS 13+, Apple Silicon или Intel. Для сборки нужны только Command Line Tools (`xcode-select --install`) и CMake (`brew install cmake`). Xcode не нужен.

```sh
./build.sh                    # universal (arm64 + x86_64): dist/WinTers_<версия>_macOS.dmg и .zip
./build.sh --arch native      # только под этот Mac, быстрее
```

Для разработки:

```sh
cmake -S . -B build/macos && cmake --build build/macos --parallel
open build/macos/WinTers.app              # выход: снежинка в строке меню → «Выйти»
build/macos/winters_preview out.png 60    # кадр через Metal-рендерер в PNG, без оверлея
```

**Подпись.** Релизы подписываются ad-hoc (без переменных окружения), поэтому пользователям при первом запуске нужно разрешить приложение (см. README).
Если понадобится подпись Developer ID и нотаризация (нужен Apple Developer Program), скрипт это уже умеет:

```sh
xcrun notarytool store-credentials winters --apple-id <apple id> --team-id <TEAMID>   # один раз
WINTERS_SIGN_IDENTITY="Developer ID Application: <Имя> (<TEAMID>)" WINTERS_NOTARY_PROFILE=winters ./build.sh
```

Bundle ID задаётся в CMake (`WINTERS_MAC_BUNDLE_ID`, по умолчанию `ru.ypklementev.WinTers`), настройки хранятся в `~/Library/Preferences/<bundle id>.plist`.

Чем macOS-версия отличается от Windows внутри:
- **Окна** берутся опросом `CGWindowListCopyWindowInfo`: событий о перемещении окон в macOS нет. Опрос идёт каждый кадр, пока зажата кнопка мыши (окно могут тащить), и каждый третий кадр в остальное время. Один опрос занимает около 0,2 мс. Разрешения не нужны: читаются только рамки окон, не заголовки.
- **Оверлей** — по одному окну `NSPanel` на каждый экран (одно окно не может накрыть несколько дисплеев, когда у них отдельные Spaces). Уровень — сразу под Dock: снег поверх всех окон приложений, а Dock, строка меню, меню и Пункт управления — поверх снега. Окно есть на всех Spaces, в том числе поверх полноэкранных приложений.
- **Координаты.** Симуляция считает в единицах «1/scale пункта», где scale — наибольший Retina-коэффициент среди экранов. На Retina это физические пиксели, как на Windows. На обычном мониторе рядом с Retina шейдеры масштабируют ширину сглаживания (`aa`).
- **Dock** в симуляцию не передаётся: снег копится внизу экрана, а Dock стоит перед сугробом и не засыпается. (Точную рамку Dock можно узнать только с разрешением Accessibility, а «полка» на всю ширину экрана выглядела неестественно.)
  Строка меню прячет верхние кромки прижатых к ней окон, как верх экрана прячет кромки развёрнутых окон на Windows.
- **Скруглённые углы окон.** Снег ложится только на прямой участок верхней кромки (`WindowInfo::cornerRadius`; на Windows 0) и осыпается с краёв под углом откоса. Радиус через публичный API не узнать: 10 pt, на macOS 26+ 16 pt.
- **Лепка:** Control + Option (любой Option: AltGr на маке нет). Пауза: Control + Option + S (Carbon `RegisterEventHotKey`).
- **Автозапуск** через `SMAppService` («Открывать при входе в систему»). Если его выключили в «Системных настройках → Основные → Объекты входа», пункт меню ведёт туда.
- **Автопауза:** режим энергосбережения (Low Power Mode), сон, погашенный экран, блокировка, переключение пользователя.
- **Шейдеры** — порт HLSL на Metal Shading Language (`src/mac/metal_shaders.h`). Они компилируются при запуске через `newLibraryWithSource`, поэтому компилятор `metal` из Xcode не нужен. **Правка шейдера на одной платформе требует такой же правки на другой.**

## Тесты симуляции

`src/sim/` написан на чистом C++ без WinAPI, поэтому его тесты собираются где угодно:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

## GitHub Actions

`.github/workflows/build.yml` запускается на push и pull request, но каждая сборка идёт только если изменились файлы, из которых она собирается (job `changes`, `dorny/paths-filter`):

| Job | Когда запускается | Что делает |
|---|---|---|
| `windows-msvc` | общий код, `src/app`, `src/overlay`, `src/render`, `src/input`, `src/windows_tracker`, `build.ps1`, `cmake/`, MSIX-пакет | `build.ps1 -Target all -TestSign`, тесты, пробная установка MSIX; артефакты **WinTers-x64** и **WinTers-msix** |
| `macos` | общий код, `src/mac`, `packaging/macos`, `build.sh`, `tools/mac_preview.mm` | `build.sh` (universal, ad-hoc подпись, тесты); артефакт **WinTers-macOS** (.dmg и .zip) |
| `sim-tests-linux` | общий код | тесты симуляции с ASan/UBSan |

Общий код — это `src/sim`, общая часть `src/render` (`instances`, `flake_atlas`, `shapes`), `tests/`, `CMakeLists.txt` и сам файл workflow.
Правка README, `docs/`, `PRIVACY.md` или `tools/*.py` ничего не собирает. Пересобрать всё вручную: Actions → build → **Run workflow**.

Пробная установка MSIX (`Add-AppxPackage`) проверяет, что манифест и подпись корректны; на раннере она может не сработать и только предупреждает.

## Проверка перед релизом

### Windows: обе сборки

1. Запустить exe (portable) или WinTers из меню «Пуск» (MSIX): в трее появилась снежинка, снег идёт **на всех мониторах**.
2. Второй запуск (в том числе portable при запущенной MSIX и наоборот) ничего не открывает.
3. Трей: пауза, Ctrl + Alt + S, режимы снегопада и ветра, дворник, «Лопата», «Как пользоваться…», ссылка на сайт, «Выход».
4. Поменять снегопад и ветер, выйти, запустить снова: настройки сохранились.
5. Включить «Запускать вместе с Windows», перезагрузиться: WinTers запустился сам. Выключить, перезагрузиться: не запустился.
6. Удаление. Portable: выключить автозапуск, выйти и удалить exe (настройки остаются в `%APPDATA%\WinTers`). MSIX: «Параметры → Приложения → WinTers → Удалить», автозапуск исчезает вместе с пакетом.

### Только MSIX

7. Установка через `Add-AppxPackage` (см. выше), запуск из «Пуск», в списке приложений правильная иконка и название.
8. Выключить WinTers в «Параметры → Приложения → Автозагрузка»: в меню трея пункт автозапуска стал неактивным с подсказкой.
9. Если до этого стояла portable-версия с автозапуском: после первого запуска MSIX в `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (regedit) **не должно** остаться значения `WinTers`.
   Если оно осталось (Windows не дала пакету удалить ключ), двойного запуска всё равно не будет: вторую копию остановит mutex.
10. Настройки portable-версии подхватились при первом запуске MSIX.

### Windows App Certification Kit

WACK входит в Windows SDK. Проверка пакета, PowerShell **от администратора**:

```powershell
$appcert = "${env:ProgramFiles(x86)}\Windows Kits\10\App Certification Kit\appcert.exe"
& $appcert reset
& $appcert test -appxpackagepath "$PWD\dist\WinTers_<версия>_x64_testsigned.msix" -reportoutputpath "$PWD\dist\wack.xml"
```

WACK устанавливает пакет, поэтому ему нужна подписанная копия с доверенным сертификатом. Итог (`OVERALL_RESULT`) в `dist\wack.xml` должен быть `PASS`.
Предупреждение про `runFullTrust` ожидаемо: для настольных приложений это нормально.

## Публикация в Microsoft Store

1. **Поднять версию** в `CMakeLists.txt`: `project(WinTers VERSION 0.1.2 ...)`. Store не принимает пакет с той же или меньшей версией, чем уже опубликованная.
2. **Собрать:** `.\build.ps1 -TestSign`.
3. **Проверить** по чек-листу выше и прогнать WACK на `_testsigned.msix`.
4. **Загрузить** в Partner Center → WinTers (9PNZ9T3SS34X) → новая отправка → «Пакеты» файл `dist\WinTers_<версия>_x64.msix` (**неподписанный**, без `_testsigned`).
5. В «Сведениях» о `runFullTrust` объяснить: приложение рисует прозрачный оверлей поверх рабочего стола и следит за положением окон, это настольное Win32-приложение.
   Политика конфиденциальности: `PRIVACY.md` в репозитории.

## Как это устроено

```
src/
  sim/              чистый C++: снежинки, поле ветра, сугробы (карты высот), видимость по z-order,
                    варежка, снежки. Без WinAPI, покрыт тестами (tests/).
  app/              wWinMain, цикл, трей и меню, settings.ini, автозапуск (Run / StartupTask), режим MSIX, автопауза, манифест, иконка
  overlay/          прозрачное click-through окно на весь виртуальный экран; список мониторов
  render/           D3D11 + DirectComposition; 4 инстансных draw call: сугробы, снежки, фигуры, снежинки;
                    общие с macOS (чистый C++, под тестами): instances — данные инстансов из World,
                    flake_atlas — процедурный атлас спрайтов, shapes — дворник и украшения снеговика
  windows_tracker/  окна верхнего уровня: EnumWindows + DWM-границы + SetWinEventHook
  input/            курсор и клавиши опросом каждый кадр (без хуков), Ctrl+Alt+S
  mac/              macOS: контроллер и меню в строке меню, оверлеи NSPanel, Metal-рендерер и MSL-шейдеры,
                    экраны и координаты, CGWindowList, ввод и Control+Option+S, NSUserDefaults и SMAppService
packaging/          шаблон AppxManifest.xml и логотипы MSIX; macos/ — Info.plist и WinTers.icns
tools/make_icon.py  генератор src/app/snowflake.ico; make_store_assets.py — логотипы MSIX; make_icns.py — иконка macOS
tools/mac_preview.mm  кадр симуляции через Metal-рендерер в PNG (проверка шейдеров без оверлея)
build.ps1           сборка portable exe и MSIX в dist/ (Windows)
build.sh            сборка WinTers.app, .dmg и .zip в dist/ (macOS)
```

- **Оверлей.** Окно `WS_EX_NOREDIRECTIONBITMAP | TRANSPARENT | LAYERED | TOPMOST | TOOLWINDOW | NOACTIVATE`.
  Swap chain создаётся через `CreateSwapChainForComposition` (`DXGI_ALPHA_MODE_PREMULTIPLIED`). `UpdateLayeredWindow` не используется.
  В режиме лепки у окна временно снимается `WS_EX_TRANSPARENT`.
- **Цикл.** Один поток. `MsgWaitForMultipleObjectsEx` ждёт сообщения и высокоточный таймер с частотой 60 Гц.
  Симуляция идёт с фиксированным шагом 1/60 с. Кадр рисуется, когда waitable-объект swap chain готов (frame latency = 1).
  На паузе нет ни таймера, ни кадров, и CPU почти не тратится.
- **Снежинки.** Пул частиц в формате SoA. Скорость каждой частицы релаксирует к скорости воздуха плюс её скорость падения; скорость подстройки зависит от размера.
  Воздух — функция тока из нескольких мод: получаются настоящие вихри без источников и стоков.
  Поле считается на сетке 64 px; моды разделимы по осям, поэтому тригонометрии на сетку уходит O(nx + ny).
  Для каждой снежинки значение берётся билинейной интерполяцией.
  Покачивание и кувыркание задаёт фазор, который поворачивается заранее посчитанным шагом, без sin/cos на частицу.
  Масса снега отделена от размера частицы, а обломки несут ровно ту массу, которую сняли.
  При ветре снег входит через наветренный край экрана, а не из невидимой полосы над экраном.
- **Сугробы.** У каждого окна есть одномерная карта высот с шагом 2 px × масштаб.
  Снежинка ложится, когда пересекает линию снега на видимом участке кромки. Видимый участок — это кромка минус окна выше по z-order минус области вне мониторов.
  Края развёрнутых окон у верха экрана не считаются.
  Склоны сглаживаются до угла откоса. Излишек с краёв и в «дыры» превращается в падающие частицы.
- **Трекинг окон.** Учитываются только видимые окна, кроме свёрнутых, cloaked (другие рабочие столы), tool windows, click-through и полностью прозрачных.
  Панели задач (`Shell_TrayWnd`, `Shell_SecondaryTrayWnd`) учитываются всегда.
  Список пересобирается по событиям SetWinEventHook и не реже раза в 500 мс.
- **Координаты.** Физические пиксели (Per-Monitor DPI Aware V2 через манифест).
- **Шейдеры** компилируются при запуске через `d3dcompiler_47.dll`, который входит в Windows 10+.
- **Восстановление.** Окно и устройство пересоздаются при смене мониторов, перемещении панели задач, потере D3D-устройства и после сна или разблокировки.
- **Нагрузка.** Симуляция на двух мониторах (1440p + 1080p) с 25 окнами занимает около 0,3% одного ядра в «лёгком» режиме, 1,3% в «ровном» и около 7% в метели (~23 000 частиц).
