# Отклонения эталонной сборки от оригинальных исходников

Оригинал — `jsj2008/lithtech`, ветка `master`, `tools/LTC` + `libs/ltamgr` + `sdk/inc`.
Ниже **полный** список всего, что эталонная сборка делает иначе. Всё остальное —
неприкосновенные файлы из апстрима (копии в `src/`, права Monolith/Touchdown, см. `NOTICE.md`).

## 1. Две правки в исходниках (помечены `//[gcc]` в коде)

Обе — механические, вызваны тем, что MSVC продлевал область видимости переменной
цикла до конца блока (нестандартное расширение). Семантика не меняется.

| файл | что | почему |
|---|---|---|
| `src/ltamgr/ltahuffmantree.cpp` (`IncrementWeight`) | `uint32 nTestNode` поднят из `for(...)` в блок | `nTestNode` использовался после цикла |
| `src/LTC/main.cpp` (скан расширения) | `int32 nCurrChar` объявлен перед циклом | то же |

Файл `ltahuffmantree.cpp` в рантайме **не участвует** (мёртвый код, §6.7 ANALYSIS-4),
но линкуется, поэтому должен компилироваться.

## 2. Шимы (`shim/`), подключаются только в эталонной сборке

| файл | что делает |
|---|---|
| `wincompat.h` | `<strings.h>`, `<cstring>`, `<cstdint>`; подключается через `-include` |
| `iostream.h`, `fstream.h` | MSVC-написания → `using namespace std` + современный заголовок |
| `tdguard_impl.cpp` | стаб `TdGuard::Aegis` вместо `sdk/lib/TdGuard.lib` (Win32 copy-protection); `Init()/DoWork()` → `true`, на вывод не влияет |

**Запрещено:** `#define stricmp strcasecmp` — `ltbasedefs.h` под `__LINUX` сам объявляет
`inline int stricmp()`, макрос переписал бы имя этого объявления и дал бесконечную
рекурсию (проверено ASan: stack overflow на первом вызове).

## 3. Флаги компилятора

```
-std=gnu++17 -w -fpermissive -D__LINUX -DLINUX
-I. -Isrc/LTC -Isrc/ltamgr -Isrc/sdk/inc -Icompat -Ishim -include shim/wincompat.h
```

* `-fpermissive` — `sdk/inc/physics/coordinate_frame.h` использует зависимые имена,
  которые современный gcc разрешает только в два прохода.
* `-w` — исходники 1999 года, предупреждений тысячи; эталон собирается «как есть».
* `-D__LINUX -DLINUX` — штатные ветки в `ltbasedefs.h`/`ltbasetypes.h`.

## 4. Разрешение инклюдов (`compat/`, генерируется)

`ltamgr.h` и другие заголовки подключают `"..\ltamgr\ltafile.h"`; gcc считает обратные
слэши символами имени файла. `autocompat.py` компилирует TU, ловит
`fatal error: X: No such file or directory`, находит файл по basename без учёта регистра
в `src/` и кладёт **относительный** симлинк `compat/X` с точным написанием из исходника
(включая backslash-варианты и `ILTAAllocator.h` → `iltaallocator.h`).

Каталог `compat/` — производный: `build.sh` пересоздаёт его на чистом дереве, в git не идёт.

## 5. Что НЕ делалось

UB-фиксов нет. Единственный известный кандидат (`m_DecWnd` декодера не обнуляется,
§6.6 ANALYSIS-4) на корректных файлах недостижим и в эталон не вносится — иначе эталон
перестал бы быть копией оригинала. Решение для порта принимается на M1 и фиксируется в
`docs/porting-notes.md`.
