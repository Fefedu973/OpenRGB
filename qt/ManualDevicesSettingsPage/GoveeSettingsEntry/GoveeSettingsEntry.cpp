/*---------------------------------------------------------*\
| GoveeSettingsEntry.cpp                                    |
|                                                           |
|   User interface for OpenRGB Govee settings entry         |
|                                                           |
|   Adam Honse (calcprogrammer1@gmail.com)      15 May 2025 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "GoveeSettingsEntry.h"
#include "ui_GoveeSettingsEntry.h"
#include "GoveeDiscovery.h"

GoveeSettingsEntry::GoveeSettingsEntry(QWidget *parent) :
    BaseManualDeviceEntry(parent),
    ui(new Ui::GoveeSettingsEntry)
{
    ui->setupUi(this);
}

GoveeSettingsEntry::~GoveeSettingsEntry()
{
    delete ui;
}

void GoveeSettingsEntry::changeEvent(QEvent *event)
{
    if(event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
    }
}

void GoveeSettingsEntry::loadFromSettings(const json& data)
{
    if(data.contains("ip"))
    {
        ui->IPEdit->setText(QString::fromStdString(data["ip"]));
    }
    if(data.contains("mac") && data["mac"].is_string())
    {
        ui->MacEdit->setText(QString::fromStdString(data["mac"]));
    }
}

json GoveeSettingsEntry::saveSettings()
{
    json result;
    result["ip"] = ui->IPEdit->text().toStdString();
    if(!ui->MacEdit->text().trimmed().isEmpty())
        result["mac"] = GoveeDiscovery::NormalizeMac(ui->MacEdit->text().trimmed().toStdString());
    return result;
}

bool GoveeSettingsEntry::isDataValid()
{
    return GoveeDiscovery::ValidIPv4(ui->IPEdit->text().toStdString()) &&
        (ui->MacEdit->text().trimmed().isEmpty() ||
         !GoveeDiscovery::NormalizeMac(ui->MacEdit->text().trimmed().toStdString()).empty());
}

static BaseManualDeviceEntry* SpawnGoveeSettingsEntry(const json& data)
{
    GoveeSettingsEntry* entry = new GoveeSettingsEntry;
    entry->loadFromSettings(data);
    return entry;
}

REGISTER_MANUAL_DEVICE_TYPE("Govee", "GoveeDevices", SpawnGoveeSettingsEntry);
