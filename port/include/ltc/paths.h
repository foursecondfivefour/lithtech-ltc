// Имя выхода и определение сжатости — порт CLTAUtil::IsFileCompressed и того куска
// main.cpp, который собирает имя выходного файла.
#pragma once

#include <string>

namespace ltc {

// Q3: сжатость определяется ТОЛЬКО расширением, без учёта регистра, по последней
// точке в имени. Нет точки -> false. `a.b.ltc` -> true, `a.ltc.bak` -> false.
bool is_file_compressed(const char* filename);

enum class NameResult
{
    kOk,
    kBadExtension
};

// Собирает имя выхода, когда -out не задан: ищет ПОСЛЕДНЮЮ точку во всём пути
// (включая каталоги — так в оригинале), проверяет, что остаток равен .lta/.ltc
// без учёта регистра, и заменяет расширение на противоположное в нижнем регистре.
//
// Q5: оригинал копирует результат в char[_MAX_PATH] (256) через strcpy — это UB на
// длинных путях. Порт возвращает std::string без ограничения длины: единственное
// отличие — на путях длиннее 255 символов порт работает вместо неопределённого
// поведения. Зафиксировано в docs/porting-notes.md.
NameResult derive_output_name(const char* input, bool input_compressed, std::string& out);

// Расширение для противоположной формы файла.
const char* opposite_extension(bool input_compressed);

} // namespace ltc
