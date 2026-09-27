QT += core
QT -= gui
CONFIG += console c++17 release
CONFIG -= app_bundle debug debug_and_release
TEMPLATE = app
TARGET = color_frames_test
INCLUDEPATH += stubs ../../RGBController ../../dependencies/json ../..
SOURCES += test_color_frames.cpp ../../RGBController/RGBController.cpp ../../RGBController/RGBControllerKeyNames.cpp ../../StringUtils.cpp
