// No process logger or hardware manager is linked by this test harness.
#pragma once
#include <nlohmann/json.hpp>
using json = nlohmann::json;
constexpr unsigned LL_WARNING = 3;
#define LOG_ERROR(...) ((void)0)
