// CLI-поток инструмента — порт tools/LTC/main.cpp.
//
// Порядок проверок, тексты сообщений, коды возврата и порядок открытия файлов
// повторяют оригинал; всё это зафиксировано в docs/ANALYSIS-4.md §3 и проверяется
// дифференциальным гейтом scripts/cli_parity.sh.
#pragma once

#include <string>
#include <vector>

namespace ltc {

// коды возврата: 0 — успех, 1 — ошибка (как в оригинале), 2 — не реализовано в порте
inline constexpr int kExitOk             = 0;
inline constexpr int kExitError          = 1;
inline constexpr int kExitNotImplemented = 2;

// argv без имени программы (то, что оригинал передаёт как argc-1, argv+1)
int run_args(const std::vector<std::string>& args);

// argv[0] пропускается, как в оригинале
int run(int argc, char** argv);

} // namespace ltc
