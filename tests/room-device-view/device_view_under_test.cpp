// Compile the exact widget source while exposing its geometry to the harness.
// MSVC encodes access level in member symbols, so this must match the test TU.
#include <QPainter>
#include <QResizeEvent>
#include <QStyleOption>
#include <QtCore/qmath.h>
#include <QDebug>
#include <QMouseEvent>
#include <QWidget>
#include <cmath>
#include <limits>
#include "RGBController.h"
#include "ResourceManager.h"
#include "RGBControllerKeyNames.h"
#define private public
#include "../../qt/DeviceView.cpp"
#undef private
