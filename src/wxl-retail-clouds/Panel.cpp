// The clouds' knobs, on screen where the sky is.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "wxl-retail-clouds/Clouds.hpp"

#include "config.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/ui/ImGuiHost.hpp"

#include "imgui.h"

namespace
{
    void Help(const char* text)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void Draw(void*)
    {
        wxl::clouds::CloudTuning& g = wxl::clouds::Tuning();
        bool dirty = false;

        dirty |= ImGui::Checkbox("Enabled", &g.enabled);
        Help("Off returns the sky to the engine's own generated pattern -- the comparison every "
             "other number here is judged against. The dome, its colours and its density stay the "
             "zone's either way.");

        dirty |= ImGui::SliderFloat("Scale", &g.scale, 1.0f, 8.0f, "%.1f");
        Help("Cells of the broadest octave across the sheet. Low is a sky with a few great masses "
             "in it; high is scattered flock.");

        dirty |= ImGui::SliderInt("Octaves", &g.octaves, 2, 6);
        Help("Detail layers summed, each half the size of the last. More is finer curls for a "
             "little more CPU on the rows being regrown.");

        dirty |= ImGui::SliderFloat("Coverage bias", &g.coverageBias, -0.5f, 0.5f, "%.2f");
        Help("Leans on the zone's own cloud coverage without replacing it. Negative is cloudier, "
             "positive is clearer; zero leaves the zone's sky exactly as authored.");

        dirty |= ImGui::SliderFloat("Sharpness", &g.sharpness, 0.5f, 6.0f, "%.2f");
        Help("How hard a cloud's edge turns on. Low is haze, high is cut paper.");

        dirty |= ImGui::SliderFloat("Warp", &g.warp, 0.0f, 1.0f, "%.2f");
        Help("How far a broad field twists every lookup before the pattern is read. Straight "
             "noise past a threshold makes blobs; cauliflower is a twisted domain, and this is "
             "the twist.");

        dirty |= ImGui::SliderFloat("Cotton", &g.cotton, 0.0f, 1.0f, "%.2f");
        Help("Folds each octave into rounded puffs instead of open grain. Zero is plain fbm; one "
             "is soap foam; the middle is weather.");

        ImGui::SliderFloat("Drift", &g.drift, 0.0f, 4.0f, "%.2f");
        Help("Scales how far each pattern step travels. The sheet advances a step at a time, "
             "exactly as the stock one does -- this is stride length, not smoothness.");

        dirty |= ImGui::SliderFloat("Depth", &g.depth, 0.0f, 1.0f, "%.2f");
        Help("How deeply a cloud shades itself: a short light-march through the field toward the "
             "sun, baked into the sheet. Bases darken under thick cloud, sun-side flanks stay "
             "lit, and the forms read as volumes instead of stains. Follows the real sun -- noon "
             "clouds come out evenly lit on their own.");

        dirty |= ImGui::SliderFloat("Silver lining", &g.lining, 0.0f, 2.0f, "%.2f");
        Help("Thin edges facing the sun glow -- the one highlight every eye knows a cloud by.");

        dirty |= ImGui::SliderFloat("Ambient", &g.ambient, 0.0f, 1.0f, "%.2f");
        Help("The floor under the self-shading. Even the shadowed base of a cloud is lit by the "
             "whole sky; zero is charcoal smoke, not weather.");

        if (ImGui::Button("Redraw sheet") || dirty)
            wxl::clouds::RequestRedraw();
        Help("Regrows the whole sheet next frame through the engine's own full-redraw flag, "
             "instead of waiting for the rolling update to come round. Slider changes do this on "
             "their own.");
    }

    bool Install()
    {
        wxl::ui::AddPanel("Clouds", &Draw, nullptr, 380.0f, 0.0f);
        return true;
    }
}

WXL_REGISTER_FEATURE("retail-clouds-panel", wxl::features::retailClouds, Install)
