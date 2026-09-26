/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QWidget>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "RGBController.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
// Inspect widget geometry for validation; production APIs remain unchanged.
#define private public
#include "DeviceView.h"
#undef private
#include "ImageOutputView.h"

#define CHECK(value) do { if(!(value)) throw std::runtime_error(#value); } while(0)

class FakeController : public RGBController
{
public:
    FakeController(unsigned width, unsigned height)
    {
        name = "Synthetic device, no transport";
        leds.resize(width * height);
        for(auto& pixel : leds) pixel.name = "Pixel";
        zone area;
        area.name = "Synthetic matrix";
        area.type = ZONE_TYPE_MATRIX;
        area.leds_count = area.leds_min = area.leds_max = width * height;
        area.matrix_map.width = width;
        area.matrix_map.height = height;
        area.matrix_map.map.resize(width * height);
        std::iota(area.matrix_map.map.begin(), area.matrix_map.map.end(), 0U);
        zones.push_back(area);
        SetupColors();
        SetAllColors(ToRGBColor(80, 140, 200));
    }
    ~FakeController() override { Shutdown(); }
};

class FakeImageController : public FakeController, public room_image::RGBControllerImageInterface
{
public:
    FakeImageController() : FakeController(2,1)
    {
        zone second;
        second.name = "Portrait output";
        zones.push_back(second);
        auto pixels = std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{0,0,255,255, 255,0,0,255});
        auto frame = std::make_shared<room_image::Frame>();
        frame->width = 2; frame->height = 1; frame->stride = 8; frame->sequence = 1; frame->pixels = pixels;
        current = frame;
    }
    mutable int reads = 0;
    bool available = true;
    room_image::Mapping mapping;
    std::shared_ptr<const room_image::Frame> current;
    bool GetImageOutput(unsigned zone, room_image::Output& output) const override
    {
        if(zone > 1) return false;
        output = {zone, zone == 0 ? 800U : 120U, zone == 0 ? 600U : 800U, 30};
        return true;
    }
    room_image::SubmitResult SubmitImage(unsigned, std::shared_ptr<const room_image::Frame>, const room_image::Mapping&, unsigned) override
    { throw std::runtime_error("Preview attempted to write to its controller"); }
    bool GetImagePreview(unsigned, std::shared_ptr<const room_image::Frame>& frame, room_image::Mapping& view) const override
    { ++reads; frame = current; view = mapping; return available; }
};

void ImagePreviewContract()
{
    FakeImageController controller;
    ImageOutputView view(&controller);
    CHECK(view.HasOutputs() && view.OutputCount() == 2);
    auto* picture = view.findChild<QLabel*>("imageOutputPreview");
    auto* selector = view.findChild<QComboBox*>("imageOutputSelector");
    auto* status = view.findChild<QLabel*>("imageOutputStatus");
    CHECK(picture && selector && status);
    QTest::qWait(90);
    CHECK(controller.reads == 0); // hidden pages do no preview work
    view.show();
    QTest::qWait(90);
    CHECK(controller.reads > 0);
    QImage preview = picture->pixmap().toImage();
    CHECK(preview.size() == QSize(240,180));
    CHECK(preview.pixelColor(0,90).red() > 240 && preview.pixelColor(239,90).blue() > 240);
    const auto unchanged_key = picture->pixmap().cacheKey();
    QTest::qWait(90);
    CHECK(picture->pixmap().cacheKey() == unchanged_key);
    controller.mapping = room_image::Mapping::Rectangle(0,0,1,1,0,true,false);
    QTest::qWait(90);
    CHECK(picture->pixmap().cacheKey() != unchanged_key);
    preview = picture->pixmap().toImage();
    CHECK(preview.pixelColor(0,90).blue() > 240);
    selector->setCurrentIndex(1);
    preview = picture->pixmap().toImage();
    CHECK(preview.width() <= 320 && preview.height() <= 180 && preview.height() > preview.width());
    controller.mapping.brightness = std::numeric_limits<double>::quiet_NaN();
    QTest::qWait(90);
    CHECK(picture->pixmap().isNull() && status->text().contains("Invalid"));
    controller.available = false;
    QTest::qWait(90);
    CHECK(picture->pixmap().isNull() && status->text().contains("expired"));
    controller.available = true;
    controller.mapping = {};
    auto invalid = std::make_shared<room_image::Frame>(*controller.current);
    invalid->stride = 10000000;
    controller.current = invalid;
    QTest::qWait(90);
    CHECK(picture->pixmap().isNull() && status->text().contains("Invalid"));
    view.hide();
    const int previous_reads = controller.reads;
    QTest::qWait(90);
    CHECK(controller.reads == previous_reads);
    FakeController ordinary(2,1);
    ImageOutputView unsupported(&ordinary);
    CHECK(!unsupported.HasOutputs() && unsupported.OutputCount() == 0);
}

void Prepare(DeviceView& view, RGBController& controller, int width = 800, int height = 600)
{
    view.SetController(&controller);
    view.SetDisableKeyExpansion(true);
    view.resize(width, height);
    view.show();
    QApplication::processEvents();
}

void GeometryAndMouse()
{
    FakeController controller(4, 2);
    DeviceView view;
    Prepare(view, controller, 400, 220);
    const auto valid = view.led_pos[0];
    const int saved_size = view.size;
    view.size = 100;
    view.offset_x = 5;
    view.led_pos[0] = {0.123f, 0.456f, 0.25f, 0.15f};
    CHECK(view.LEDRect(0) == QRect(static_cast<int>(0.123f * 100 + 5), static_cast<int>(0.456f * 100), 25, 15));
    view.led_pos[0] = {0.2f, 0.2f, 0.0001f, 0.0001f};
    CHECK(view.LEDRect(0) == QRect(25, 20, 1, 1));
    for(const float invalid : {0.0f, -0.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        view.led_pos[0].matrix_w = invalid;
        CHECK(view.LEDRect(0).isEmpty());
        view.led_pos[0] = {0.2f, 0.2f, 0.0001f, invalid};
        CHECK(view.LEDRect(0).isEmpty());
        view.led_pos[0] = {0.2f, 0.2f, 0.0001f, 0.0001f};
    }
    view.led_pos[0].matrix_x = std::numeric_limits<float>::infinity();
    CHECK(view.LEDRect(0).isEmpty());
    view.led_pos[0] = valid;
    view.size = saved_size;
    view.InitDeviceView();
    const QRect rect = view.LEDRect(3);
    CHECK(!rect.isEmpty());
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, rect.center());
    CHECK(view.selected_leds == std::vector<unsigned>{3});
    QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier, view.LEDRect(5).center());
    CHECK(view.selected_leds == (std::vector<unsigned>{3, 5}));
    QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier, rect.center());
    CHECK(view.selected_leds == std::vector<unsigned>{5});
    CHECK(view.SelectLEDs({5,3,5}));
    CHECK(view.selected_leds == (std::vector<unsigned>{3,5}));
    CHECK(!view.SelectLEDs({3,8}));
    CHECK(view.selected_leds == (std::vector<unsigned>{3,5}));
    CHECK(view.SelectLEDs({}));
    CHECK(view.selected_leds.empty());
    view.led_pos[0] = {0, 0, 0, 0};
    view.selection_rect = view.rect();
    view.ctrl_down = false;
    view.UpdateSelection();
    CHECK(!view.selection_flags[0]);
}

void RasterMatchesPainter()
{
    FakeController ordinary(8, 1);
    FakeController many(5000, 1);
    DeviceView reference;
    DeviceView raster;
    Prepare(reference, ordinary, 128, 128);
    Prepare(raster, many, 128, 128);
    const matrix_pos_size_type positions[] = {
        {0.05f,0.05f,0.001f,0.001f}, {0.05f,0.05f,0.001f,0.001f},
        {0.10f,0.10f,0.20f,0.20f}, {0.15f,0.15f,0.001f,0.001f},
        {0.45f,0.45f,0.001f,0.001f}, {0.40f,0.40f,0.20f,0.20f},
        {-0.01f,0.20f,0.04f,0.02f}, {0.01f,0.01f,0,0}
    };
    const RGBColor colors[] = {ToRGBColor(255,0,0), ToRGBColor(0,0,255), ToRGBColor(0,255,0),
        ToRGBColor(255,255,0), ToRGBColor(255,0,0), ToRGBColor(0,0,255), ToRGBColor(100,50,220), 0};
    reference.size = raster.size = 100;
    reference.offset_x = raster.offset_x = 0;
    reference.matrix_h = raster.matrix_h = 1;
    reference.zone_pos.assign(1, matrix_pos_size_type{});
    raster.zone_pos = reference.zone_pos;
    std::fill(raster.led_pos.begin(), raster.led_pos.end(), matrix_pos_size_type{});
    for(unsigned i = 0; i < 8; ++i)
    {
        reference.led_pos[i] = raster.led_pos[i] = positions[i];
        ordinary.SetColor(i, colors[i]);
        many.SetColor(i, colors[i]);
    }
    // Interleave tiny and normal LEDs, including overlaps and partially clipped
    // rectangles. Transparent highlight must flush and use normal composition.
    reference.selection_flags[3] = raster.selection_flags[3] = true;
    for(int alpha : {255, 117})
    {
        QPalette palette = reference.palette();
        palette.setColor(QPalette::Highlight, QColor(180, 50, 220, alpha));
        reference.setPalette(palette);
        raster.setPalette(palette);
        for(const QRegion region : {QRegion(QRect(0,0,128,128)), QRegion(QRect(3,3,35,40))})
        {
            QImage expected(128,128,QImage::Format_ARGB32_Premultiplied);
            QImage actual(expected.size(),expected.format());
            expected.fill(Qt::white);
            actual.fill(Qt::white);
            reference.render(&expected,QPoint(),region);
            raster.render(&actual,QPoint(),region);
            CHECK(expected == actual);
        }
    }
    CHECK(!raster.preview_raster.isNull());
    CHECK(raster.preview_raster.sizeInBytes() <= 4 * 1024 * 1024 * 4);
    FakeController empty(0,0);
    DeviceView empty_view;
    Prepare(empty_view,empty,128,128);
    CHECK(empty_view.LEDRect(0).isEmpty());
    CHECK(std::isfinite(empty_view.matrix_h));
}

QJsonObject DenseBenchmark()
{
    FakeController controller(800, 600);
    DeviceView view;
    QElapsedTimer clock;
    clock.start();
    Prepare(view, controller);
    const double init_ms = clock.nsecsElapsed() / 1e6;
    CHECK(view.led_labels.empty());
    const unsigned selected_index = 300 * 800 + 400;
    const QRect pixel = view.LEDRect(selected_index);
    CHECK(pixel.width() == 1 && pixel.height() == 1);
    clock.restart();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, pixel.center());
    const double click_ms = clock.nsecsElapsed() / 1e6;
    CHECK(std::find(view.selected_leds.begin(), view.selected_leds.end(), selected_index) != view.selected_leds.end());

    QImage image(view.QWidget::size(), QImage::Format_ARGB32_Premultiplied);
    std::vector<double> times;
    for(int iteration = 0; iteration < 12; ++iteration)
    {
        clock.restart();
        view.render(&image);
        times.push_back(clock.nsecsElapsed() / 1e6);
    }
    std::sort(times.begin(), times.end());
    // No changing content or hardware: benchmark only real widget raster paint.
    QJsonObject result;
    result["leds"] = 480000;
    result["widget_width"] = view.width();
    result["widget_height"] = view.height();
    result["initialization_and_first_paint_ms"] = init_ms;
    result["click_ms"] = click_ms;
    result["paint_median_ms"] = times[times.size() / 2];
    result["paint_max_ms"] = times.back();
    result["selected_at_one_pixel"] = static_cast<int>(view.selected_leds.size());
    result["capture_or_hardware_io"] = false;
    return result;
}

void SnapshotConcurrency()
{
    FakeController controller(200, 100);
    std::atomic<bool> running{true};
    std::thread writer([&]
    {
        unsigned value = 1;
        while(running.load()) controller.SetAllZoneColors(0, value++);
    });
    std::vector<RGBColor> snapshot;
    bool coherent = true;
    for(int i = 0; i < 100; ++i)
    {
        controller.CopyColorsSnapshot(snapshot);
        coherent = coherent && snapshot.size() == 20000
            && std::all_of(snapshot.begin(), snapshot.end(), [&](RGBColor value) { return value == snapshot.front(); });
    }
    running.store(false);
    writer.join();
    CHECK(coherent);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        GeometryAndMouse();
        RasterMatchesPainter();
        SnapshotConcurrency();
        ImagePreviewContract();
        const auto result = DenseBenchmark();
        const auto json = QJsonDocument(result).toJson(QJsonDocument::Indented);
        std::cout << json.constData();
        QFile report("device-view-benchmark.json");
        CHECK(report.open(QIODevice::WriteOnly));
        report.write(json);
        std::cout << "PASS: actual DeviceView offscreen, actual RGBController snapshots, no hardware linked.\n";
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
