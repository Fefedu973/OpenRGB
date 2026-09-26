// RGBController's destructor diagnostic is the only logging dependency here.
#pragma once
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#define LOG_ERROR(...) ((void)0)
