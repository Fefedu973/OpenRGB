/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "RGBController.h"
#include "FrameRouting/RGBControllerImageInterface.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <limits>
#include <vector>

/* GUI-only bounded preview of any optional image output. The controller must
 * outlive this widget, exactly as it outlives its containing device page.
 * No transport operation or full-frame copy is performed by this widget. */
class ImageOutputView : public QWidget
{
public:
    explicit ImageOutputView(RGBController* controller, QWidget* parent = nullptr)
        : QWidget(parent), device(controller), image_interface(dynamic_cast<room_image::RGBControllerImageInterface*>(controller))
    {
        auto* layout = new QVBoxLayout(this);
        selector = new QComboBox(this);
        selector->setObjectName(QStringLiteral("imageOutputSelector"));
        selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        selector->setMinimumContentsLength(24);
        picture = new QLabel(this);
        picture->setObjectName(QStringLiteral("imageOutputPreview"));
        picture->setAlignment(Qt::AlignCenter);
        picture->setMinimumSize(160, 90);
        picture->setMaximumSize(320, 180);
        status = new QLabel(this);
        status->setObjectName(QStringLiteral("imageOutputStatus"));
        status->setWordWrap(true);
        layout->addWidget(selector);
        layout->addWidget(picture, 0, Qt::AlignHCenter);
        layout->addWidget(status);
        connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
        {
            ClearPreview();
            if(isVisible()) RefreshPreview();
        });
        auto* timer = new QTimer(this);
        timer->setInterval(67); // <=15 Hz; no frame/provider work for hidden pages
        connect(timer, &QTimer::timeout, this, [this]
        {
            if(!isVisible()) return;
            if(!outputs_checked.isValid() || outputs_checked.elapsed() >= 1000) RefreshOutputs();
            RefreshPreview();
        });
        RefreshOutputs();
        timer->start();
    }

    bool HasOutputs() const { return !outputs.empty(); }
    unsigned OutputCount() const { return static_cast<unsigned>(outputs.size()); }

    void RefreshOutputs()
    {
        std::vector<Entry> available;
        if(device && image_interface)
        {
            const unsigned count = device->GetZoneCount();
            for(unsigned zone = 0; zone < count; ++zone)
            {
                room_image::Output output;
                if(!image_interface->GetImageOutput(zone, output) || output.zone != zone
                   || output.width > 16384 || output.height > 16384) continue;
                const QString name = QString::fromStdString(device->GetZoneDisplayName(zone));
                available.push_back({output, name});
            }
        }
        outputs_checked.restart();
        bool same = available.size() == outputs.size();
        for(std::size_t i = 0; same && i < available.size(); ++i)
        {
            const auto& a = available[i];
            const auto& b = outputs[i];
            same = a.output.zone == b.output.zone && a.output.width == b.output.width
                && a.output.height == b.output.height && a.output.max_fps == b.output.max_fps && a.name == b.name;
        }
        if(same && !outputs.empty()) return;
        const int old_index = selector->currentIndex();
        const unsigned old_zone = old_index >= 0 && static_cast<std::size_t>(old_index) < outputs.size()
            ? outputs[old_index].output.zone : std::numeric_limits<unsigned>::max();
        outputs = std::move(available);
        const QSignalBlocker blocker(selector);
        selector->clear();
        int next_index = 0;
        for(std::size_t index = 0; index < outputs.size(); ++index)
        {
            const auto& entry = outputs[index];
            const QString name = entry.name.isEmpty() ? tr("Zone %1").arg(entry.output.zone + 1) : entry.name;
            const QString dimensions = entry.output.width && entry.output.height
                ? QStringLiteral("%1 × %2").arg(entry.output.width).arg(entry.output.height) : tr("native size unspecified");
            selector->addItem(name + QStringLiteral(" — ") + dimensions, entry.output.zone);
            if(entry.output.zone == old_zone) next_index = static_cast<int>(index);
        }
        selector->setEnabled(HasOutputs());
        if(HasOutputs()) selector->setCurrentIndex(next_index);
        ClearPreview();
    }

private:
    struct Entry { room_image::Output output; QString name; };
    RGBController* device = nullptr;
    room_image::RGBControllerImageInterface* image_interface = nullptr;
    QComboBox* selector = nullptr;
    QLabel* picture = nullptr;
    QLabel* status = nullptr;
    QElapsedTimer outputs_checked;
    std::vector<Entry> outputs;
    std::shared_ptr<const room_image::Frame> last_frame;
    room_image::Mapping last_mapping;
    QSize last_size;
    unsigned last_zone = std::numeric_limits<unsigned>::max();
    QImage thumbnail;

    static bool SameMapping(const room_image::Mapping& a, const room_image::Mapping& b)
    {
        return a.origin_x == b.origin_x && a.origin_y == b.origin_y && a.u_x == b.u_x && a.u_y == b.u_y
            && a.v_x == b.v_x && a.v_y == b.v_y && a.brightness == b.brightness;
    }

    void ClearPreview(const QString& message = QString())
    {
        last_frame.reset();
        last_zone = std::numeric_limits<unsigned>::max();
        picture->clear();
        status->setText(message.isEmpty()
            ? (HasOutputs() ? tr("No image available — source absent or expired") : tr("No native image output")) : message);
    }

    void RefreshPreview()
    {
        if(!isVisible() || !image_interface) return;
        const int index = selector->currentIndex();
        if(index < 0 || static_cast<std::size_t>(index) >= outputs.size()) return;
        const auto output = outputs[index].output;
        std::shared_ptr<const room_image::Frame> frame;
        room_image::Mapping mapping;
        if(!image_interface->GetImagePreview(output.zone, frame, mapping))
        {
            if(last_frame || status->text() != tr("No image available — source absent or expired")) ClearPreview();
            return;
        }
        if(!frame || !frame->Valid() || !mapping.Valid())
        {
            ClearPreview(tr("Invalid image or mapping"));
            return;
        }
        const QSize native_size(output.width ? output.width : frame->width, output.height ? output.height : frame->height);
        const QSize size = native_size.scaled(320, 180, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        if(last_frame && last_zone == output.zone && last_size == size && SameMapping(last_mapping, mapping)
           && last_frame->pixels == frame->pixels && last_frame->sequence == frame->sequence
           && last_frame->width == frame->width && last_frame->height == frame->height && last_frame->stride == frame->stride) return;
        if(thumbnail.size() != size) thumbnail = QImage(size, QImage::Format_RGB32);
        if(thumbnail.isNull()) { ClearPreview(tr("Preview allocation failed")); return; }
        for(int y = 0; y < size.height(); ++y)
        {
            auto* row = reinterpret_cast<QRgb*>(thumbnail.scanLine(y));
            for(int x = 0; x < size.width(); ++x)
                row[x] = room_image::SampleBGRA(*frame, mapping, (x + 0.5) / size.width(), (y + 0.5) / size.height());
        }
        picture->setPixmap(QPixmap::fromImage(thumbnail));
        status->setText(tr("Source %1 × %2 · preview %3 × %4 · frame %5")
            .arg(frame->width).arg(frame->height).arg(size.width()).arg(size.height()).arg(frame->sequence));
        last_frame = std::move(frame);
        last_mapping = mapping;
        last_size = size;
        last_zone = output.zone;
    }
};
