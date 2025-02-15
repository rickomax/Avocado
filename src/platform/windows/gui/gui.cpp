#include "gui.h"
#include <fmt/core.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <platform/windows/utils/platform_tools.h>
#include <utils/free_cam.h>

#include "config.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl.h"
#include "platform/windows/input/key.h"
#include "platform/windows/gui/icons.h"
#include "system.h"
#include "state/state.h"
#include "utils/file.h"
#include "utils/string.h"
#include "utils/screenshot.h"
#include "utils/gameshark.h"
#include "images.h"

float GUI::scale = 1.f;

GUI::GUI(SDL_Window* window, void* glContext) : window(window) {
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    iniPath = avocado::PATH_USER + "imgui.ini";
    io.IniFilename = iniPath.c_str();
//    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
#ifdef ANDROID
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
#endif

    scale = 1.f;
#ifdef ANDROID
    float dpi = 1.f;
    if (SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) == 0) {
        scale = dpi / 160.f;
    }
#endif
    float fontSize = 16.f * scale;

    ImGuiStyle& style = ImGui::GetStyle();
    style.GrabRounding = 6.f;
    style.FrameRounding = 6.f;
    style.ScaleAllSizes(scale);
#ifdef ANDROID
    style.TouchExtraPadding = ImVec2(10.f, 10.f);
#endif

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = fontSize;

    {
        auto font = getFileContents(avocado::assetsPath("roboto-mono.ttf"));
        io.Fonts->AddFontFromMemoryTTF(font.data(), font.size(), fontSize, &config);
    }

    {
        ImFontConfig config;
        config.SizePixels = 13.f * scale;
        io.Fonts->AddFontDefault(&config);
    }

    {
        auto font = getFileContents(avocado::assetsPath("fa-solid-900.ttf"));
        const char* glyphs[] = {
            ICON_FA_PLAY, ICON_FA_PAUSE, ICON_FA_SAVE, ICON_FA_COMPACT_DISC, ICON_FA_GAMEPAD, ICON_FA_COMPRESS, ICON_FA_COG, ICON_FA_SYNC,
        };

        ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;
        for (auto glyph : glyphs) {
            builder.AddText(glyph);
        }
        builder.BuildRanges(&ranges);
        io.Fonts->AddFontFromMemoryTTF(font.data(), font.size(), fontSize, &config, ranges.Data);
        io.Fonts->Build();
    }

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init();

    busToken = bus.listen<Event::Gui::Debug::OpenDrawListWindows>([&](auto) {
        gpuDebug.logWindowOpen = true;
        gpuDebug.vramWindowOpen = true;
    });
}

GUI::~GUI() {
    bus.unlistenAll(busToken);
    ImGui::DestroyContext();
}

void GUI::processEvent(SDL_Event* e) { ImGui_ImplSDL2_ProcessEvent(e); }

void GUI::mainMenu(std::unique_ptr<System>& sys) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        ImGui::MenuItem("Open", nullptr, &openFile.openWindowOpen);
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
        ImGui::Separator();
        if (ImGui::MenuItem("Open Avocado directory")) {
            openFileBrowser(avocado::PATH_USER);
        }
#endif
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Esc")) bus.notify(Event::File::Exit{});
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Emulation")) {
        if (ImGui::MenuItem("Pause/Resume", Key(config.hotkeys["toggle_pause"]).getButton().c_str())) {
            if (sys->state == System::State::pause) {
                sys->state = System::State::run;
            } else if (sys->state == System::State::run) {
                sys->state = System::State::pause;
            }
        }
        if (ImGui::MenuItem("Soft reset", Key(config.hotkeys["reset"]).getButton().c_str())) {
            bus.notify(Event::System::SoftReset{});
        }

        std::string key = "Shift+";
        key += Key(config.hotkeys["reset"]).getButton();
        if (ImGui::MenuItem("Hard reset", key.c_str())) {
            bus.notify(Event::System::HardReset{});
        }

        const char* shellStatus = sys->cdrom->getShell() ? "Close disk tray" : "Open disk tray";
        if (ImGui::MenuItem(shellStatus, Key(config.hotkeys["close_tray"]).getButton().c_str())) {
            sys->cdrom->toggleShell();
        }

        if (ImGui::MenuItem("Single frame", Key(config.hotkeys["single_frame"]).getButton().c_str())) {
            singleFrame = true;
            sys->state = System::State::run;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Quick save", Key(config.hotkeys["quick_save"]).getButton().c_str())) {
            bus.notify(Event::System::SaveState{});
        }

        bool quickLoadStateExists = fs::exists(state::getStatePath(sys.get()));
        if (ImGui::MenuItem("Quick load", Key(config.hotkeys["quick_load"]).getButton().c_str(), nullptr, quickLoadStateExists))
            bus.notify(Event::System::LoadState{});

        if (ImGui::BeginMenu("Save")) {
            for (int i = 1; i <= 5; i++) {
                if (ImGui::MenuItem(fmt::format("Slot {}##save", i).c_str())) bus.notify(Event::System::SaveState{i});
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Load")) {
            bool anySaveExists = false;
            for (int i = 1; i <= 5; i++) {
                auto path = state::getStatePath(sys.get(), i);
                if (fs::exists(path)) {
                    anySaveExists = true;
                    if (ImGui::MenuItem(fmt::format("Slot {}##load", i).c_str())) bus.notify(Event::System::LoadState{i});
                }
            }

            if (!anySaveExists) {
                ImGui::TextUnformatted("No save states");
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("System log", nullptr, &sys->debugOutput)) {
            config.debug.log.system = (int)sys->debugOutput;
        }
        if (ImGui::MenuItem("Syscall log", nullptr, (bool*)&sys->biosLog)) {
            config.debug.log.bios = sys->biosLog;
        }
#ifdef ENABLE_IO_LOG
        ImGui::MenuItem("IO log", nullptr, &ioDebug.logWindowOpen);
#endif
        ImGui::MenuItem("GTE log", nullptr, &gteDebug.logWindowOpen);
        ImGui::MenuItem("GPU log", nullptr, &gpuDebug.logWindowOpen);

        ImGui::Separator();

        ImGui::MenuItem("Debugger", nullptr, &cpuDebug.debuggerWindowOpen);
        ImGui::MenuItem("Breakpoints", nullptr, &cpuDebug.breakpointsWindowOpen);
        ImGui::MenuItem("Watch", nullptr, &cpuDebug.watchWindowOpen);
        ImGui::MenuItem("RAM", nullptr, &cpuDebug.ramWindowOpen);

        ImGui::Separator();

        ImGui::MenuItem("CDROM", nullptr, &cdromDebug.cdromWindowOpen);
        ImGui::MenuItem("Timers", nullptr, &timersDebug.timersWindowOpen);
        ImGui::MenuItem("GPU", nullptr, &gpuDebug.registersWindowOpen);
        ImGui::MenuItem("GTE", nullptr, &gteDebug.registersWindowOpen);
        ImGui::MenuItem("SPU", nullptr, &spuDebug.spuWindowOpen);
        ImGui::MenuItem("VRAM", nullptr, &gpuDebug.vramWindowOpen);
        ImGui::MenuItem("Kernel", nullptr, &showKernelWindow);

        ImGui::Separator();
        if (ImGui::MenuItem("Dump state")) {
            sys->dumpRam();
            sys->spu->dumpRam();
            sys->gpu->dumpVram("vram.png");
            toast("State dumped");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Options")) {
        if (ImGui::MenuItem("Graphics", nullptr)) showGraphicsOptionsWindow = true;
        ImGui::MenuItem("BIOS", nullptr, &biosOptions.biosWindowOpen);
        if (ImGui::MenuItem("Controller", nullptr)) showControllerSetupWindow = true;
        if (ImGui::MenuItem("Hotkeys", nullptr)) showHotkeysSetupWindow = true;
        ImGui::MenuItem("Memory Card", nullptr, &memoryCardOptions.memoryCardWindowOpen);

        bool soundEnabled = config.options.sound.enabled;
        if (ImGui::MenuItem("Sound", nullptr, &soundEnabled)) {
            config.options.sound.enabled = soundEnabled;
            bus.notify(Event::Config::Spu{});
        }

        ImGui::Separator();

        bool preserveState = config.options.emulator.preserveState;
        if (ImGui::MenuItem("Save state on close", nullptr, &preserveState)) {
            config.options.emulator.preserveState = preserveState;
        }

        bool timeTravel = config.options.emulator.timeTravel;
        if (ImGui::MenuItem("Time travel", Key(config.hotkeys["rewind_state"]).getButton().c_str(), &timeTravel)) {
            config.options.emulator.timeTravel = timeTravel;
        }

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Free Camera")) {
        FreeCamera* free_camera = FreeCamera::getInstance();
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        ImGui::MenuItem("Enabled", nullptr, &free_camera->enabled);
        ImGui::PopItemFlag();
        ImGui::Separator();
        ImGui::SliderInt("Base Rotation X", &free_camera->base_rotationX, -180, 180);
        ImGui::SliderInt("Base Rotation Y", &free_camera->base_rotationY, -180, 180);
        ImGui::SliderInt("Base Rotation Z", &free_camera->base_rotationZ, -180, 180);
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        if (ImGui::Button("Set Current Rotation as Base")) {
            free_camera->saveBase();
        }
        ImGui::PopItemFlag();
        ImGui::Separator();
        ImGui::SliderInt("Speed", &free_camera->cameraSpeed, 1, 500);
        ImGui::Separator();
        ImGui::SliderInt("Rotation X", &free_camera->rotationX, -180, 180);
        ImGui::SliderInt("Rotation Y", &free_camera->rotationY, -180, 180);
        ImGui::SliderInt("Rotation Z", &free_camera->rotationZ, -180, 180);
        ImGui::InputInt("Translation X", &free_camera->translationX);
        ImGui::InputInt("Translation Y", &free_camera->translationY);
        ImGui::InputInt("Translation Z", &free_camera->translationZ);
        ImGui::SliderInt("Projection Distance", &free_camera->translationH, -1000, 1000);
        ImGui::Separator();
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        if (ImGui::Button("Reset")) {
            free_camera->resetMatrices();
        }
        ImGui::PopItemFlag();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("3D Screenshot")) {
        Screenshot* screenshot = Screenshot::getInstance();
        ImGui::SliderInt("Frames to Capture", &screenshot->captureFrames, 1, 20);
        ImGui::Separator();
        ImGui::SliderInt("Horizontal Scale (%)", &screenshot->horizontalScale, 1, 1000);
        ImGui::SliderInt("Vertical Scale (%)", &screenshot->verticalScale, 1, 1000);
        ImGui::SliderInt("Depth Scale (%)", &screenshot->depthScale, 1, 1000);
        ImGui::Separator();
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        ImGui::MenuItem("Debug Mode", nullptr, &screenshot->debug);
        ImGui::PopItemFlag();
        ImGui::Separator();
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        ImGui::MenuItem("Apply Aspect Ratio", nullptr, &screenshot->applyAspectRatio);
        ImGui::PopItemFlag();
        if (screenshot->applyAspectRatio) {
            ImGui::Value("Aspect Ratio",
                         (float)sys->gpu.get()->gp1_08.getHorizontalResoulution() / (float)sys->gpu.get()->gp1_08.getVerticalResoulution());
            ImGui::Separator();
        }
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        ImGui::MenuItem("Vertex Only", nullptr, &screenshot->vertexOnly);
        ImGui::PopItemFlag();
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
        ImGui::MenuItem("Original Positions (No Perspective/Distortion)", nullptr, &screenshot->dontTransform);
        ImGui::PopItemFlag();
        if (screenshot->dontTransform && !screenshot->vertexOnly) {
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Dont Project Triangles (UV Discarded)", nullptr, &screenshot->dontProject);
            ImGui::PopItemFlag();
            ImGui::Separator();
        }
        if (!screenshot->vertexOnly) {
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Flip Projected X", nullptr, &screenshot->flipX);
            ImGui::PopItemFlag();
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Flip Projected Y", nullptr, &screenshot->flipY);
            ImGui::PopItemFlag();
            // ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            // ImGui::MenuItem("Capture Flat Triangles (Unprojected 2D Triangles)", nullptr, &screenshot->captureFlatTris);
            // ImGui::PopItemFlag();
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Group Objects", nullptr, &screenshot->groupObjects);
            ImGui::PopItemFlag();
            if (screenshot->groupObjects) {
                ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                ImGui::MenuItem("Connect to Groups only", nullptr, &screenshot->connectToGroups);
                ImGui::PopItemFlag();
            }
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Try to Connect to Processed Triangles", nullptr, &screenshot->connectToSubGroups);
            ImGui::PopItemFlag();
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Disable Backface Culling", nullptr, &screenshot->doubleSided);
            ImGui::PopItemFlag();
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Disable Fog", nullptr, &screenshot->disableFog);
            ImGui::PopItemFlag();
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            ImGui::MenuItem("Save Images as BMP", nullptr, &screenshot->bmpImages);
            ImGui::PopItemFlag();
            if (!screenshot->bmpImages) {
                ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                ImGui::MenuItem("Black is Transparent", nullptr, &screenshot->blackIsTrans);
                ImGui::PopItemFlag();
            }
            // ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            // ImGui::MenuItem("Experimental Capture (Tries to Capture Vertices that don't go thru Projection)", nullptr,
            //                &screenshot->experimentalCapture);
            // ImGui::PopItemFlag();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Take 3D Screenshot", nullptr, nullptr)) {
            screenshotSelectDirectory.windowOpen = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Cheats")) {
        Gameshark* gameshark = Gameshark::getInstance();
        if (gameshark->gamesharkCheats.size()) {
            for (auto it = gameshark->gamesharkCheats.begin(); it != gameshark->gamesharkCheats.end(); it++) {
                ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
                ImGui::MenuItem(it->name.c_str(), nullptr, &it->enabled);
                ImGui::PopItemFlag();
            }
            ImGui::Separator();
            ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
            if (ImGui::Button("Clear Cheats")) {
                gameshark->gamesharkCheats.clear();
            }
            ImGui::PopItemFlag();
        } else {
            ImGui::Text("No Cheats Loaded");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About", nullptr, &aboutHelp.aboutWindowOpen);
        ImGui::EndMenu();
    }

    std::string info;
    if (statusMouseLocked) {
        info += " | Press Alt to unlock mouse";
    }
    if (sys->state == System::State::pause) {
        info += " | Paused";
    } else {
        info += fmt::format(" | {:.0f} FPS", statusFps);
        if (!statusFramelimitter) {
            info += " (Unlimited)";
        }
    }
    auto size = ImGui::CalcTextSize(info.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - size.x - ImGui::GetStyle().FramePadding.x * 4);
    ImGui::TextUnformatted(info.c_str());

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(fmt::format("Frame time: {:.2f} ms\nTab to disable frame limiting", (1000.0 / statusFps)).c_str());
        ImGui::EndTooltip();
    }
    ImGui::EndMainMenuBar();
}

void GUI::render(std::unique_ptr<System>& sys) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    if (ImGui::BeginPopupModal("Avocado", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Avocado needs to be set up before running.");
        ImGui::Text("You need one of BIOS files placed in data/bios directory.");

        if (ImGui::Button("Select BIOS file")) {
            biosOptions.biosWindowOpen = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (showGui) {
        if (showMenu) mainMenu(sys);

        // File
        openFile.displayWindows();

        // Screenshot
        screenshotSelectDirectory.displayWindows();

        // Debug
        if (showKernelWindow) kernelWindow(sys.get());

        cdromDebug.displayWindows(sys.get());
        cpuDebug.displayWindows(sys.get());
        gpuDebug.displayWindows(sys.get());
        timersDebug.displayWindows(sys.get());
        gteDebug.displayWindows(sys.get());
        spuDebug.displayWindows(sys.get());
        ioDebug.displayWindows(sys.get());

        // Options
        if (showGraphicsOptionsWindow) graphicsOptionsWindow();
        if (showControllerSetupWindow) controllerSetupWindow();
        if (showHotkeysSetupWindow) hotkeysSetupWindow();

        biosOptions.displayWindows();
        memoryCardOptions.displayWindows(sys.get());

        // Help
        aboutHelp.displayWindows();
    }

    toasts.display();

    if (!config.isEmulatorConfigured() && !notInitializedWindowShown) {
        notInitializedWindowShown = true;
        ImGui::OpenPopup("Avocado");
    } else if (droppedItem) {
        if (!droppedItemDialogShown) {
            droppedItemDialogShown = true;
            ImGui::OpenPopup("Disc");
        }
    } else {
        drawControls(sys);
    }

    if (droppedItem) {
        discDialog();
    }

    // Work in progress
    //    renderController();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUI::discDialog() {
    if (!ImGui::BeginPopupModal("Disc", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("%s", getFilenameExt(*droppedItem).c_str());
    if (ImGui::Button("Boot")) {
        bus.notify(Event::File::Load{*droppedItem, Event::File::Load::Action::slowboot});
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart console and boot the CD");

    ImGui::SameLine();
    if (ImGui::Button("Fast boot")) {
        bus.notify(Event::File::Load{*droppedItem, Event::File::Load::Action::fastboot});
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart console and boot the CD (skipping BIOS intro)");

    ImGui::SameLine();
    if (ImGui::Button("Swap disc")) {
        bus.notify(Event::File::Load{*droppedItem, Event::File::Load::Action::swap});
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Swap currently inserted disc");
    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    if (!ImGui::IsPopupOpen("Disc")) {
        droppedItem = {};
        droppedItemDialogShown = false;
    }
}

void GUI::drawControls(std::unique_ptr<System>& sys) {
    auto symbolButton = [](const char* hint, const char* symbol, ImVec4 bg = ImVec4(1, 1, 1, 0.08f)) -> bool {
        auto padding = ImGui::GetStyle().FramePadding;
        padding.x *= 3.f;
        padding.y *= 2.5f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[2]);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(bg.x, bg.y, bg.z, bg.w));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bg.x, bg.y, bg.z, 0.5));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(bg.x, bg.y, bg.z, 0.75));

        bool clicked = ImGui::Button(symbol);

        ImGui::PopStyleColor(3);
        ImGui::PopFont();
        ImGui::PopStyleVar();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", hint);
        }

        return clicked;
    };
    auto io = ImGui::GetIO();

    static auto lastTime = std::chrono::steady_clock::now();
    // Reset timer if mouse was moved
    if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
        lastTime = std::chrono::steady_clock::now();
    }

    auto now = std::chrono::steady_clock::now();
    float timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count() / 1000.f;

    const float visibleFor = 1.5f;       // Seconds
    const float fadeoutDuration = 0.5f;  // Seconds

    // Display for 1.5 seconds, then fade out 0.5 second, then hide
    float alpha;
    if (timeDiff < visibleFor) {
        alpha = 1.f;
    } else if (timeDiff < visibleFor + fadeoutDuration) {
        alpha = lerp(1.f, 0.f, (timeDiff - visibleFor) / fadeoutDuration);
    } else {
        return;
    }

    // TODO: Handle gamepad "home" button

    auto displaySize = io.DisplaySize;
    auto pos = ImVec2(displaySize.x / 2.f, displaySize.y * 0.9f);
    ImGui::SetNextWindowPos(pos, 0, ImVec2(.5f, .5f));

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
    ImGui::Begin("##controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);

    ImVec4 pauseColor = ImVec4(1, 0, 0, 0.25f);
    if (sys->state == System::State::run) {
        if (symbolButton("Pause emulation", ICON_FA_PAUSE, pauseColor)) {
            sys->state = System::State::pause;
        }
    } else {
        if (symbolButton("Resume emulation", ICON_FA_PLAY, pauseColor)) {
            sys->state = System::State::run;
        }
    }

    ImGui::SameLine();

    symbolButton("Save/Load", ICON_FA_SAVE);
    if (ImGui::BeginPopupContextItem(nullptr, 0)) {
        if (fs::exists(state::getStatePath(sys.get()))) {
            if (ImGui::Selectable("Quick load")) bus.notify(Event::System::LoadState{});
            ImGui::Separator();
        }
        if (ImGui::Selectable("Quick save")) bus.notify(Event::System::SaveState{});
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    std::string game{};

    if (sys->cdrom->disc) {
        auto cdPath = sys->cdrom->disc->getFile();
        if (!cdPath.empty()) {
            game = getFilename(cdPath);
        }
    }
    if (!game.empty()) {
        ImGui::TextUnformatted(game.c_str());
    } else {
        if (symbolButton("Open", ICON_FA_COMPACT_DISC, ImVec4(0.25f, 0.5f, 1, 0.4f))) {
            openFile.openWindowOpen = true;
        }
    }

    ImGui::SameLine();

    symbolButton("Settings", ICON_FA_COG);
    if (ImGui::BeginPopupContextItem(nullptr, 0)) {
        if (ImGui::Selectable("Controller")) showControllerSetupWindow = !showControllerSetupWindow;
        ImGui::Separator();
        ImGui::MenuItem("Show menu", Key(config.hotkeys["toggle_menu"]).getButton().c_str(), &showMenu);
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    std::string hint = "Toggle fullscreen (" + (std::string)Key(config.hotkeys["toggle_fullscreen"]).getButton().c_str() + ")";
    if (symbolButton(hint.c_str(), ICON_FA_COMPRESS)) {
        bus.notify(Event::Gui::ToggleFullscreen{});
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void GUI::renderController() {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    float size = 64.f;
    auto button = [drawList, size](const char* button, float _x, float _y) {
        auto btn = getImage(button, avocado::assetsPath("buttons/"));
        if (!btn) return;
        // AddImage(ImTextureID user_texture_id, const ImVec2& p_min, const ImVec2& p_max, const ImVec2& uv_min = ImVec2(0, 0), const
        // ImVec2& uv_max = ImVec2(1, 1), ImU32 col = IM_COL32_WHITE);
        float x = ImGui::GetIO().DisplaySize.x * _x;
        float y = ImGui::GetIO().DisplaySize.y * lerp(0.3f, 0.6f, _y);
        float r = size / 2 * 1.2;

        drawList->AddCircleFilled(ImVec2(x, y), r, ImColor(0, 0, 0, 128));
        drawList->AddImage((ImTextureID)btn->id, ImVec2(x - size / 2, y - size / 2), ImVec2(x + size / 2, y + size / 2), ImVec2(0, 0),
                           ImVec2(1, 1), ImColor(0xff, 0xff, 0xff, 192));
    };

    float COL = 1.f / 12.f;
    float ROW = 1.f / 3.f;

    button("dpad_up", 2 * COL, 1 * ROW);
    button("dpad_left", 1 * COL, 2 * ROW);
    button("dpad_right", 3 * COL, 2 * ROW);
    button("dpad_down", 2 * COL, 3 * ROW);

    button("select", 5 * COL, 3 * ROW);
    button("start", 7 * COL, 3 * ROW);

    button("triangle", 1 - 2 * COL, 1 * ROW);
    button("square", 1 - 1 * COL, 2 * ROW);
    button("circle", 1 - 3 * COL, 2 * ROW);
    button("cross", 1 - 2 * COL, 3 * ROW);
}
