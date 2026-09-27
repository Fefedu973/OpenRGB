// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QFile>
#include <QSaveFile>
#include <QString>
#include <functional>
#include <nlohmann/json.hpp>
#include "ProfileLoadState.h"

// GUI-owned store. No device state, preset files, or global configuration is
// written here. QSaveFile keeps the previous checkpoint intact on failure.
class LastSessionCheckpoint
{
public:
    explicit LastSessionCheckpoint(QString filename) : path(std::move(filename)) {}

    bool Read(nlohmann::json& result) const
    {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly) || file.size() > MaximumBytes) return false;
        const auto bytes = file.readAll();
        auto parsed = nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false);
        if(!Valid(parsed)) return false;
        result = std::move(parsed);
        return true;
    }

    bool Capture(bool ready, const ProfileLoadState& load_state,
                 const std::function<nlohmann::json()>& collect,
                 const std::string& source_profile)
    {
        if(!ready || load_state.Busy()) return false;
        const auto revision = load_state.Revision();
        auto plugins = collect();
        if(load_state.Busy() || load_state.Revision() != revision) return false;
        nlohmann::json state = {{"version", 1}, {"plugins", std::move(plugins)},
                                {"source_profile", source_profile}};
        if(!Valid(state)) return false;
        if(state == previous) return true;
        const std::string bytes = state.dump(2) + "\n";
        if(bytes.size() > MaximumBytes) return false;
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly) ||
           file.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) ||
           !file.commit()) return false;
        previous = std::move(state);
        ++writes;
        return true;
    }

    unsigned WriteCount() const { return writes; }
    static bool Valid(const nlohmann::json& state)
    {
        if(!state.is_object() || state.size() != 3 || !state.contains("version") ||
           state["version"] != 1 || !state.contains("plugins") ||
           !state["plugins"].is_object() || state["plugins"].empty() ||
           !state.contains("source_profile") || !state["source_profile"].is_string()) return false;
        return true;
    }

private:
    static constexpr qint64 MaximumBytes = 8 * 1024 * 1024;
    QString path;
    nlohmann::json previous;
    unsigned writes = 0;
};
