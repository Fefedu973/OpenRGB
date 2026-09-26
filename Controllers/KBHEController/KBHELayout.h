/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <array>
#include "RGBControllerKeyNames.h"
#include "KBHEProtocol.h"

/* Geometry from Fefe-Nayz/kbhe-monorepo integrations/signalrgb/KEY_LAYOUT.
 * 1/8-key-unit grid retains ISO Enter, offsets, arrows and wide-key centers.
 * These indices are firmware logical keys K01..K82, not WS2812 wire order.
 * Standard OpenRGB names describe physical positions, not Windows key events. */
namespace KBHELayout
{
constexpr unsigned int WIDTH = 130;
constexpr unsigned int HEIGHT = 52;
struct Key { const char* name; unsigned int column; unsigned int row; };
const std::array<Key, KBHEProtocol::LED_COUNT> KEYS = {{
    {KEY_EN_ESCAPE, 4, 4}, // K01
    {KEY_EN_F1, 14, 4}, // K02
    {KEY_EN_F2, 22, 4}, // K03
    {KEY_EN_F3, 30, 4}, // K04
    {KEY_EN_F4, 38, 4}, // K05
    {KEY_EN_F5, 48, 4}, // K06
    {KEY_EN_F6, 56, 4}, // K07
    {KEY_EN_F7, 64, 4}, // K08
    {KEY_EN_F8, 72, 4}, // K09
    {KEY_EN_F9, 82, 4}, // K10
    {KEY_EN_F10, 90, 4}, // K11
    {KEY_EN_F11, 98, 4}, // K12
    {KEY_EN_F12, 106, 4}, // K13
    {KEY_EN_DELETE, 116, 4}, // K14
    {KEY_EN_BACK_TICK, 4, 14}, // K15
    {KEY_EN_1, 12, 14}, // K16
    {KEY_EN_2, 20, 14}, // K17
    {KEY_EN_3, 28, 14}, // K18
    {KEY_EN_4, 36, 14}, // K19
    {KEY_EN_5, 44, 14}, // K20
    {KEY_EN_6, 52, 14}, // K21
    {KEY_EN_7, 60, 14}, // K22
    {KEY_EN_8, 68, 14}, // K23
    {KEY_EN_9, 76, 14}, // K24
    {KEY_EN_0, 84, 14}, // K25
    {KEY_EN_MINUS, 92, 14}, // K26
    {KEY_EN_EQUALS, 100, 14}, // K27
    {KEY_EN_BACKSPACE, 112, 14}, // K28
    {KEY_EN_PAGE_UP, 126, 14}, // K29
    {KEY_EN_TAB, 6, 22}, // K30
    {KEY_EN_Q, 16, 22}, // K31
    {KEY_EN_W, 24, 22}, // K32
    {KEY_EN_E, 32, 22}, // K33
    {KEY_EN_R, 40, 22}, // K34
    {KEY_EN_T, 48, 22}, // K35
    {KEY_EN_Y, 56, 22}, // K36
    {KEY_EN_U, 64, 22}, // K37
    {KEY_EN_I, 72, 22}, // K38
    {KEY_EN_O, 80, 22}, // K39
    {KEY_EN_P, 88, 22}, // K40
    {KEY_EN_LEFT_BRACKET, 96, 22}, // K41
    {KEY_EN_RIGHT_BRACKET, 104, 22}, // K42
    {KEY_EN_ISO_ENTER, 115, 26}, // K43
    {KEY_EN_PAGE_DOWN, 126, 22}, // K44
    {KEY_EN_CAPS_LOCK, 7, 30}, // K45
    {KEY_EN_A, 18, 30}, // K46
    {KEY_EN_S, 26, 30}, // K47
    {KEY_EN_D, 34, 30}, // K48
    {KEY_EN_F, 42, 30}, // K49
    {KEY_EN_G, 50, 30}, // K50
    {KEY_EN_H, 58, 30}, // K51
    {KEY_EN_J, 66, 30}, // K52
    {KEY_EN_K, 74, 30}, // K53
    {KEY_EN_L, 82, 30}, // K54
    {KEY_EN_SEMICOLON, 90, 30}, // K55
    {KEY_EN_QUOTE, 98, 30}, // K56
    {KEY_EN_POUND, 106, 30}, // K57
    {KEY_EN_HOME, 126, 30}, // K58
    {KEY_EN_LEFT_SHIFT, 5, 38}, // K59
    {KEY_EN_ISO_BACK_SLASH, 14, 38}, // K60
    {KEY_EN_Z, 22, 38}, // K61
    {KEY_EN_X, 30, 38}, // K62
    {KEY_EN_C, 38, 38}, // K63
    {KEY_EN_V, 46, 38}, // K64
    {KEY_EN_B, 54, 38}, // K65
    {KEY_EN_N, 62, 38}, // K66
    {KEY_EN_M, 70, 38}, // K67
    {KEY_EN_COMMA, 78, 38}, // K68
    {KEY_EN_PERIOD, 86, 38}, // K69
    {KEY_EN_FORWARD_SLASH, 94, 38}, // K70
    {KEY_EN_RIGHT_SHIFT, 105, 38}, // K71
    {KEY_EN_UP_ARROW, 118, 40}, // K72
    {KEY_EN_LEFT_CONTROL, 5, 46}, // K73
    {KEY_EN_LEFT_WINDOWS, 15, 46}, // K74
    {KEY_EN_LEFT_ALT, 25, 46}, // K75
    {KEY_EN_SPACE, 55, 46}, // K76
    {KEY_EN_RIGHT_ALT, 84, 46}, // K77
    {KEY_EN_RIGHT_FUNCTION, 92, 46}, // K78
    {KEY_EN_RIGHT_CONTROL, 100, 46}, // K79
    {KEY_EN_LEFT_ARROW, 110, 48}, // K80
    {KEY_EN_DOWN_ARROW, 118, 48}, // K81
    {KEY_EN_RIGHT_ARROW, 126, 48}, // K82
}};
}
