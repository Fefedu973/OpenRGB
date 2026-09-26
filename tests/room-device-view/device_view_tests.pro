QT += core gui widgets testlib
CONFIG += console c++17 release
CONFIG -= app_bundle debug debug_and_release
TEMPLATE = app
TARGET = device-view-tests
INCLUDEPATH += stubs ../../qt ../../RGBController ../../dependencies/json ../../
SOURCES += test_device_view.cpp device_view_under_test.cpp ../../RGBController/RGBController.cpp ../../RGBController/RGBControllerKeyNames.cpp ../../StringUtils.cpp
HEADERS += ../../qt/DeviceView.h
