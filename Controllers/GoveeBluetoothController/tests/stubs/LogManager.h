// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone probe logging boundary, not used by the production OpenRGB build.
#pragma once
#include <cstdio>
#define LOG_INFO(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while(0)
#define LOG_WARNING(...) LOG_INFO(__VA_ARGS__)
#define LOG_ERROR(...) LOG_INFO(__VA_ARGS__)
