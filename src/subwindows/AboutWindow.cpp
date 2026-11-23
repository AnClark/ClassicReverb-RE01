#include "UI.h"
#include "config.h"

void ClassicReverbUI::_drawAboutWindow()
{
    // ── "About" window (fullscreen) ───────────────────────────────────────────────
    static constexpr auto about_window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (!fAboutWindowOpened)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    if (ImGui::Begin("About Window", &fAboutWindowOpened, about_window_flags))
    {
        // Main area
        {
            ImGui::Columns(2, "AboutColumns", false);
            ImGui::SetColumnWidth(0, SCALE(400.0f - 5.0f));
            ImGui::SetColumnWidth(1, SCALE(420.0f - 15.0f));

            {
                const String versionStr = String("Classic Reverb RE-01") + "  |  Version " +
                                    String(VERSION_MAJOR) + "." +
                                    String(VERSION_MINOR) + "." +
                                    String(VERSION_PATCH);

                ImGui::SeparatorText(versionStr);
                ImGui::Text("Reverse engineering of Kjaerhus Audio Classic Reverb (2003).");
                ImGui::Text("Original algorithm by Kjaerhus Audio.");
                ImGui::Text("Copyright (c) 2026 AnClark Liu <clarklaw4701@qq.com>");
                
                ImGui::SeparatorText("License: GNU General Public License v3.0 or later");
                ImGui::Dummy(ImVec2(0, SCALE(2)));
                ImGui::TextWrapped("Classic Reverb RE-01 is free software: "
                                        "you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,"
                                        "either version 3 of the License, or (at your option) any later version.");
            }

            ImGui::NextColumn();

            {
                ImGui::SeparatorText("Disclaimer");
                ImGui::TextWrapped("This is an unofficial, reverse-engineered clone of the discontinued Kjaerhus Classic Reverb, aiming at bringing "
                                        "this vintage and fantastic plugin to life again.");
                ImGui::TextWrapped("This project is NOT related to official Kjaerhus Audio, Acoustica LLC. and their affiliates.");
                ImGui::Dummy(ImVec2(0, SCALE(2)));
                ImGui::TextWrapped("The Kjaerhus logo is used under fair use for identification purposes only, "
                                        "and is not intended to infringe any trademarks.");
                ImGui::Dummy(ImVec2(0, SCALE(2)));
                ImGui::TextWrapped("VST is a trademark of Steinberg GmbH.");
            }

            ImGui::Columns(1);
        }

        // "OK" button
        {
            ImGui::BeginGroup();
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, SCALE(5.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0x2f, 0x4d, 0x44, 0xff));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0x2f + 20, 0x4d + 20, 0x44 + 20, 0xff));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0x2f + 40, 0x4d + 40, 0x44 + 40, 0xff));

            // Fixed position OK button at bottom-right (screen coordinates)
            static const ImVec2 button_size = ImVec2(SCALE(60 - 5), SCALE(25));
            ImVec2 buttonPos = ImVec2(viewport->Pos.x + viewport->Size.x - button_size.x - SCALE(22.0f),
                                    viewport->Pos.y + viewport->Size.y - button_size.y - SCALE(10.0f));
            ImGui::SetCursorScreenPos(buttonPos);
            if (ImGui::Button("OK", button_size))
            {
                fAboutWindowOpened = false;
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(); // FrameRounding

            ImGui::EndGroup();
        }

        ImGui::End();
    }
}
