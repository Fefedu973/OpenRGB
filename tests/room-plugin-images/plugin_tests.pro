QT += core gui widgets
CONFIG += console c++17 release
CONFIG -= app_bundle debug debug_and_release
TEMPLATE = app
TARGET = plugin-image-test
CORE = $$PWD/../..
INCLUDEPATH += stubs $$CORE $$CORE/RGBController $$CORE/dependencies/json
SOURCES += test_plugins.cpp $$CORE/RGBController/RGBController.cpp $$CORE/RGBController/RGBController_Virtual.cpp $$CORE/RGBController/RGBControllerKeyNames.cpp $$CORE/StringUtils.cpp
