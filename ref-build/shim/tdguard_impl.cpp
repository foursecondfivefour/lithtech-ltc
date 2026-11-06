// TdGuard / Aegis: the original links a prebuilt Windows copy-protection
// library (sdk/lib/TdGuard.lib). It is not available for Linux and does not
// influence any output, so the reference build stubs it out.
#include "tdguard.h"

namespace TdGuard
{
Aegis::Aegis() {}
Aegis::~Aegis() {}
Aegis& Aegis::GetSingleton() { static Aegis s; return s; }
bool Aegis::Init() { return true; }
bool Aegis::DoWork() { return true; }
}
