#include "gpu.h"
#define _USE_MATH_DEFINES
#include <fmt/core.h>
#include <imgui.h>
#include <math.h>
#include <magic_enum.hpp>
#include <stb_image_write.h>
#include <device/gpu/render/texture_utils.h>
#include "config.h"
#include "platform/windows/gui/images.h"
#include "renderer/opengl/opengl.h"
#include "system.h"
#include "utils/file.h"
#include "utils/gpu_draw_list.h"

namespace gui::debug {
SaveDumpDialog::SaveDumpDialog() : FileDialog(helper::Mode::SaveFile) {
    saveFileName = "dump.gpudrawlist";
    windowName = "Save GPU dump##file_dialog";
}

bool SaveDumpDialog::isFileSupported(const helper::File &f) { return f.extension == ".gpudrawlist"; }
bool SaveDumpDialog::onFileSelected(const gui::helper::File &f) { return callback(f); }

GPU::GPU() {
    busToken = bus.listen<Event::Config::Graphics>([&](auto) {
        textureImage.release();
        vramImage.release();
    });
}

GPU::~GPU() { bus.unlistenAll(busToken); }

void GPU::registersWindow(System *sys) {
    auto &gpu = sys->gpu;

    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("GPU", &registersWindowOpen);

    int horRes = gpu->gp1_08.getHorizontalResoulution();
    int verRes = gpu->gp1_08.getVerticalResoulution();
    bool interlaced = gpu->gp1_08.interlace;
    int mode = gpu->gp1_08.videoMode == gpu::GP1_08::VideoMode::ntsc ? 60 : 50;
    int colorDepth = gpu->gp1_08.colorDepth == gpu::GP1_08::ColorDepth::bit24 ? 24 : 15;

    ImGui::Text("Display:");
    ImGui::Text("Resolution %d:%d%c @ %dHz", horRes, verRes, interlaced ? 'i' : 'p', mode);
    ImGui::Text("Color depth: %dbit", colorDepth);
    ImGui::Text("areaStart:  %4d:%4d", gpu->displayAreaStartX, gpu->displayAreaStartY);
    ImGui::Text("rangeX:     %4d:%4d", gpu->displayRangeX1, gpu->displayRangeX2);
    ImGui::Text("rangeY:     %4d:%4d", gpu->displayRangeY1, gpu->displayRangeY2);

    ImGui::Text("");
    ImGui::Text("Drawing:");
    ImGui::Text("areaMin:    %4d:%4d", gpu->drawingArea.left, gpu->drawingArea.top);
    ImGui::Text("areaMax:    %4d:%4d", gpu->drawingArea.right, gpu->drawingArea.bottom);
    ImGui::Text("offset:     %4d:%4d", gpu->drawingOffsetX, gpu->drawingOffsetY);
    // ImGui::Text("")

    ImGui::End();
}

// Helpers
namespace {
std::string getGP0CommandName(const gpu::LogEntry &entry) {
    uint8_t cmd = entry.cmd();
    if (cmd == 0x00) {
        return "nop";
    } else if (cmd == 0x01) {
        return "clear cache";
    } else if (cmd == 0x02) {
        uint16_t srcX = entry.args[1] & 0xffff;
        uint16_t srcY = (entry.args[1] >> 16) & 0xffff;

        uint16_t width = entry.args[2] & 0xffff;
        uint16_t height = (entry.args[2] >> 16) & 0xffff;
        return fmt::format("fill rectangle    [{}:{}, size {}:{}]", srcX, srcY, width, height);
    } else if (cmd == 0x1f) {
        return "requestIrq";
    } else if (cmd >= 0x20 && cmd <= 0x3f) {
        return "polygon";
    } else if (cmd >= 0x40 && cmd <= 0x5f) {
        return "line";
    } else if (cmd >= 0x60 && cmd <= 0x7f) {
        return "rectangle";
    } else if (cmd >= 0x80 && cmd <= 0x9f) {
        uint16_t srcX = entry.args[1] & 0xffff;
        uint16_t srcY = (entry.args[1] >> 16) & 0xffff;

        uint16_t dstX = entry.args[2] & 0xffff;
        uint16_t dstY = (entry.args[2] >> 16) & 0xffff;

        uint16_t width = entry.args[3] & 0xffff;
        uint16_t height = (entry.args[3] >> 16) & 0xffff;

        return fmt::format("copy vram -> vram [{}:{} -> {}:{}, size {}:{}]", srcX, srcY, dstX, dstY, width, height);
    } else if (cmd >= 0xa0 && cmd <= 0xbf) {
        uint16_t dstX = entry.args[1] & 0xffff;
        uint16_t dstY = (entry.args[1] >> 16) & 0xffff;

        uint16_t width = entry.args[2] & 0xffff;
        uint16_t height = (entry.args[2] >> 16) & 0xffff;

        return fmt::format("copy cpu -> vram  [{}:{}, size {}:{}]", dstX, dstY, width, height);
    } else if (cmd >= 0xc0 && cmd <= 0xdf) {
        uint16_t srcX = entry.args[1] & 0xffff;
        uint16_t srcY = (entry.args[1] >> 16) & 0xffff;

        uint16_t width = entry.args[2] & 0xffff;
        uint16_t height = (entry.args[2] >> 16) & 0xffff;

        return fmt::format("copy vram -> cpu  [{}:{}, size {}:{}]", srcX, srcY, width, height);
    } else if (cmd == 0xe1) {
        gpu::GP0_E1 e1;
        e1._reg = entry.args[0];

        int texPageX = e1.texturePageBaseX * 64;
        int texPageY = e1.texturePageBaseY * 256;
        auto semi = magic_enum::enum_name(e1.semiTransparency);
        auto bits = magic_enum::enum_name(e1.texturePageColors);
        auto dither = e1.dither24to15 ? "dither" : "no-dither";

        return fmt::format("set drawMode      [{}:{} {} {}, {}]", texPageX, texPageY, semi, bits, dither);
    } else if (cmd == 0xe2) {
        gpu::GP0_E2 e2;
        e2._reg = entry.args[0];

        return fmt::format("set textureWindow [mask {}:{}, offset {}:{}]", (int)e2.maskX * 8, (int)e2.maskY * 8, (int)e2.offsetX * 8,
                           (int)e2.offsetY * 8);
    } else if (cmd == 0xe3) {
        uint16_t left = entry.args[0] & 0x3ff;
        uint16_t top = (entry.args[0] & 0xffc00) >> 10;

        return fmt::format("set drawAreaBegin [{}:{}]", left, top);
    } else if (cmd == 0xe4) {
        uint16_t right = entry.args[0] & 0x3ff;
        uint16_t bottom = (entry.args[0] & 0xffc00) >> 10;
        return fmt::format("set drawAreaEnd   [{}:{}]", right, bottom);
    } else if (cmd == 0xe5) {
        int16_t drawingOffsetX = extend_sign<11>(entry.args[0] & 0x7ff);
        int16_t drawingOffsetY = extend_sign<11>((entry.args[0] >> 11) & 0x7ff);
        return fmt::format("set drawOffset    [{}:{}]", drawingOffsetX, drawingOffsetY);
    } else if (cmd == 0xe6) {
        gpu::GP0_E6 e6;
        e6._reg = entry.args[0];
        return fmt::format("set maskBit       [set:{}, check:{}]", (int)e6.setMaskWhileDrawing, (int)e6.checkMaskBeforeDraw);
    } else {
        return "UNKNOWN";
    }
}

std::string getGP1CommandName(const gpu::LogEntry &entry) {
    uint8_t cmd = entry.cmd();
    if (cmd == 0x00) {
        return "Reset GPU";
    } else if (cmd == 0x01) {
        return "Reset command buffer";
    } else if (cmd == 0x02) {
        return "Acknowledge IRQ1";
    } else if (cmd == 0x03) {
        return fmt::format("Display Enable    [{}]", entry.args[0] & 1);
    } else if (cmd == 0x04) {
        return fmt::format("DMA Direction     [{}]", entry.args[0] & 3);
    } else if (cmd == 0x05) {
        uint16_t x = entry.args[0] & 0x3ff;
        uint16_t y = (entry.args[0] >> 10) & 0x1ff;
        return fmt::format("display area start[{}:{}]", x, y);
    } else if (cmd == 0x06) {
        uint16_t x1 = entry.args[0] & 0xfff;
        uint16_t x2 = (entry.args[0] >> 12) & 0xfff;
        return fmt::format("H display range   [{}-{}]", x1, x2);
    } else if (cmd == 0x07) {
        uint16_t y1 = entry.args[0] & 0x3ff;
        uint16_t y2 = (entry.args[0] >> 10) & 0x3ff;
        return fmt::format("V display range   [{}-{}]", y1, y2);
    } else if (cmd == 0x08) {
        gpu::GP1_08 gp1_08;
        gp1_08._reg = entry.args[0];

        int w = gp1_08.getHorizontalResoulution();
        int h = gp1_08.getVerticalResoulution();
        char mode = gp1_08.interlace ? 'i' : 'p';
        auto region = magic_enum::enum_name(gp1_08.videoMode);
        int color = gp1_08.colorDepth == gpu::GP1_08::ColorDepth::bit15 ? 15 : 24;

        // Reverse-flag ignored
        return fmt::format("Display mode      [{}:{}{:c}, {}, {}bits]", w, h, mode, region, color);
    } else if (cmd == 0x09) {
        return fmt::format("Allow texture disable [{}]", entry.args[0] & 1);
    } else if (cmd >= 0x10 && cmd <= 0x1f) {
        return "Get GPU info";
    } else if (cmd == 0x20) {
        return "Special texture disable";
    } else {
        return "UNKNOWN";
    }
}

std::string getCommandName(const gpu::LogEntry &entry) {
    if (entry.type == 0)
        return getGP0CommandName(entry);
    else
        return getGP1CommandName(entry);
}

bool commandHasDetails(const gpu::LogEntry &entry) {
    if (entry.cmd() == 0xe1 || (entry.cmd() >= 0x20 && entry.cmd() < 0x80)) {
        return true;
    } else {
        return false;
    }
}

bool entryLine(int i, const gpu::LogEntry &entry, bool openable) {
    ImVec4 entryColor = ImVec4(1.f, 1.f, 1.f, 1.f);
    if (entry.type == 1) {  // GP1
        entryColor = ImVec4(0.4f, 0.4f, 0.8f, 1.f);
    } else if (!commandHasDetails(entry)) {
        entryColor = ImVec4(0.4f, 0.4f, 0.4f, 1.f);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, entryColor);

    auto lineString = fmt::format("{:3d} GP{}(0x{:02x}) {}", i, entry.type, entry.cmd(), getCommandName(entry));
    bool opened = false;
    if (openable) {
        opened
            = ImGui::TreeNodeEx((void *)(intptr_t)i,
                                ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog,
                                "%s", lineString.c_str());
    } else {
        // Hack to match TreeNode left padding (from ImGui::TreeNodeBehavior)
        auto leftPadding = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2;

        ImGui::Indent(leftPadding);
        ImGui::Selectable(lineString.c_str());
        ImGui::Unindent(leftPadding);
    }
    ImGui::PopStyleColor();

    return opened;
}

void colorBox(RGB color) {
    float fcolor[3];
    fcolor[0] = color.r / 255.f;
    fcolor[1] = color.g / 255.f;
    fcolor[2] = color.b / 255.f;
    ImGui::ColorEdit3("##color", fcolor, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoInputs);
}
};  // namespace

void GPU::handlePolygonCommand(const gpu::PolygonArgs arg, const std::vector<uint32_t> &arguments) {
    int ptr = 1;

    primitive::Triangle::Vertex v[4];
    gpu::TextureInfo tex;

    for (int i = 0; i < arg.getVertexCount(); i++) {
        v[i].pos.x = extend_sign<11>(arguments[ptr] & 0xffff);
        v[i].pos.y = extend_sign<11>((arguments[ptr++] & 0xffff0000) >> 16);

        if (!arg.isRawTexture && (!arg.gouraudShading || i == 0)) v[i].color.raw = arguments[0] & 0xffffff;
        if (arg.isTextureMapped) {
            if (i == 0) tex.palette = arguments[ptr];
            if (i == 1) tex.texpage = arguments[ptr];
            v[i].uv.x = arguments[ptr] & 0xff;
            v[i].uv.y = (arguments[ptr] >> 8) & 0xff;
            ptr++;
        }
        if (arg.gouraudShading && i < arg.getVertexCount() - 1) v[i + 1].color.raw = arguments[ptr++] & 0xffffff;
    }

    std::string flags;
    if (arg.semiTransparency) flags += "semi-transparent, ";  // TODO: print WHICH transperancy is used, magic enum
    if (arg.isTextureMapped) flags += "textured, ";           // TODO: Bits?
    if (!arg.isRawTexture) flags += "color-blended, ";
    if (arg.gouraudShading) flags += "gouraud-shaded";
    ImGui::Text("Flags: %s", flags.c_str());
    for (int i = 0; i < arg.getVertexCount(); i++) {
        auto text = fmt::format("v{}: {}x{}", i, v[i].pos.x + last_offset_x, v[i].pos.y + last_offset_y);
        if (arg.isTextureMapped) {
            text += fmt::format(", uv{}: {}x{} ", i, v[i].uv.x, v[i].uv.y);
        }
        if (arg.gouraudShading) {
            text += ", color: ";
        }
        ImGui::TextUnformatted(text.c_str());

        if (arg.gouraudShading) {
            ImGui::SameLine();
            colorBox(v[i].color);
        }
    }

    if (!arg.gouraudShading) {
        ImGui::NewLine();
        ImGui::Text("Color: ");
        ImGui::SameLine();
        colorBox(RGB{arguments[0]});
    }

    if (arg.isTextureMapped) {
        std::string textureInfo = fmt::format("Texture ({} bit)", tex.getBitcount());

        ImGui::NewLine();
        ImGui::Text("%s: ", textureInfo.c_str());
        ImGui::Text("texPage: %d:%d", tex.getBaseX(), tex.getBaseY());
        ImGui::Text("CLUT:    %d:%d", tex.getClutX(), tex.getClutY());

        if (tex.getBitcount() != 0) {
            vramAreas.push_back({"CLUT", ImVec2(tex.getClutX(), tex.getClutY()), ImVec2(tex.getBitcount(), 1)});
        }
    }
}

void GPU::handleLineCommand(const gpu::LineArgs arg, const std::vector<uint32_t> &arguments) {
    int vertexCount;
    if (!arg.gouraudShading) {
        vertexCount = arguments.size() - 1;  // ignore arg[0] aka base color
    } else {
        vertexCount = arguments.size() / 2;
    }

    std::string flags;
    if (arg.semiTransparency) flags += "semi-transparent, ";
    if (arg.gouraudShading) flags += "Gouraud-shaded, ";
    if (arg.polyLine) flags += "poly-line, ";
    ImGui::Text("Flags: %s", flags.c_str());

    int ptr = 0;
    for (int i = 0; i < vertexCount; i++) {
        uint32_t color = arg.gouraudShading ? arguments[ptr++] : arguments[0];
        int16_t x = extend_sign<11>(arguments[ptr] & 0xffff);
        int16_t y = extend_sign<11>((arguments[ptr] & 0xffff0000) >> 16);
        ptr++;

        x += last_offset_x;
        y += last_offset_y;

        colorBox(RGB{color});
        ImGui::SameLine();
        ImGui::Text("Pos: %dx%d", x, y);
    }
}

void GPU::handleRectangleCommand(const gpu::RectangleArgs arg, const std::vector<uint32_t> &arguments) {
    int16_t w = arg.getSize();
    int16_t h = arg.getSize();

    if (arg.size == 0) {
        w = extend_sign<11>(arguments[(arg.isTextureMapped ? 3 : 2)] & 0xffff);
        h = extend_sign<11>((arguments[(arg.isTextureMapped ? 3 : 2)] & 0xffff0000) >> 16);
    }

    int16_t x = extend_sign<11>(arguments[1] & 0xffff);
    int16_t y = extend_sign<11>((arguments[1] & 0xffff0000) >> 16);

    x += last_offset_x;
    y += last_offset_y;

    std::string flags;
    if (arg.semiTransparency) flags += "semi-transparent, ";
    if (arg.isTextureMapped) flags += "textured, ";
    if (!arg.isRawTexture) flags += "color-blended, ";
    ImGui::Text("Flags: %s", flags.c_str());
    ImGui::Text("Pos: %dx%d", x, y);
    ImGui::Text("size: %dx%d", w, h);

    ImGui::NewLine();
    ImGui::Text("Color: ");
    ImGui::SameLine();
    colorBox(RGB{arguments[0]});

    vramAreas.push_back({fmt::format("Rectangle {}", /*i*/ -1), ImVec2(x, y), ImVec2(w, h)});

    if (arg.isTextureMapped) {
        int texX = arguments[2] & 0xff;
        int texY = (arguments[2] & 0xff00) >> 8;
        int clutX = ((arguments[2] >> 16) & 0x3f) * 16;
        int clutY = ((arguments[2] >> 22) & 0x1ff);
        int clutColors;
        int textureWidth;
        int textureBits;

        if (last_e1.texturePageColors == gpu::GP0_E1::TexturePageColors::bit4) {
            clutColors = 16;
            textureWidth = w / 4;
            textureBits = 4;
        } else if (last_e1.texturePageColors == gpu::GP0_E1::TexturePageColors::bit8) {
            clutColors = 256;
            textureWidth = w / 2;
            textureBits = 8;
        } else {
            clutColors = 0;
            textureWidth = w;
            textureBits = 16;
        }

        int texPageX = last_e1.texturePageBaseX * 64;
        int texPageY = last_e1.texturePageBaseY * 256;

        std::string textureInfo = fmt::format("Texture ({} bit)", textureBits);

        ImGui::NewLine();
        ImGui::Text("%s: ", textureInfo.c_str());
        ImGui::Text("UV:      %d:%d", texX + texPageX, texY + texPageY);
        ImGui::Text("CLUT:    %d:%d", clutX, clutY);
        ImGui::NewLine();
        ImGui::Text("Pos:     %d:%d (raw value in draw call)", texX, texY);
        ImGui::Text("texPage: %d:%d (from latest GP0_E1)", texPageX, texPageY);

        vramAreas.push_back({textureInfo, ImVec2(texPageX + texX / (16 / textureBits), texPageY + texY), ImVec2(textureWidth, h)});

        if (clutColors != 0) {
            vramAreas.push_back({"CLUT", ImVec2(clutX, clutY), ImVec2(clutColors, 1)});
        }
    }
}

void GPU::printCommandDetails(const gpu::LogEntry &entry) {
    uint8_t command = entry.cmd();
    if (command >= 0x20 && command < 0x40) {
        handlePolygonCommand(command, entry.args);
    } else if (command >= 0x40 && command < 0x60) {
        handleLineCommand(command, entry.args);
    } else if (command >= 0x60 && command < 0x80) {
        handleRectangleCommand(command, entry.args);
    } else if (command == 0xe1) {
        gpu::GP0_E1 e1;
        e1._reg = entry.args[0];

#define ENUM_NAME(x) (std::string(magic_enum::enum_name(x)).c_str())
        ImGui::Text("Texture page base:       %d:%d", e1.texturePageBaseX * 64, e1.texturePageBaseY * 256);
        ImGui::Text("Semi transparency:       %s", ENUM_NAME(e1.semiTransparency));
        ImGui::Text("Texture bit depth:       %s", ENUM_NAME(e1.texturePageColors));
        ImGui::Text("Dither 24bit:            %d", e1.dither24to15);
        ImGui::Text("Drawing to display area: %s", ENUM_NAME(e1.drawingToDisplayArea));

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.f));
        ImGui::Text("Texture disable:         %d", e1.textureDisable);
        ImGui::Text("Rect texture X flip:     %d", e1.texturedRectangleXFlip);
        ImGui::Text("Rect texture Y flip:     %d", e1.texturedRectangleYFlip);
        ImGui::PopStyleColor();
    }
}

void GPU::logWindow(System *sys) {
    vramAreas.clear();
    if (!textureImage) {
        textureImage = std::make_unique<Texture>(512, 512, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false);
        textureUnpacked.resize(512 * 512 * 4);
    }

    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("GPU Log", &logWindowOpen);

    ImGui::BeginChild("GPU Log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    last_e1 = sys->gpu->gp0_e1;
    last_offset_x = sys->gpu->drawingOffsetX;
    last_offset_y = sys->gpu->drawingOffsetY;

    int renderTo = -1;
    ImGuiListClipper clipper((int)sys->gpu.get()->gpuLogList.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            auto &entry = sys->gpu.get()->gpuLogList[i];

            bool nodeOpen = entryLine(i, entry, commandHasDetails(entry));
            bool isHovered = ImGui::IsItemHovered();

            if (isHovered) {
                renderTo = i;
            }

            if (nodeOpen) {
                printCommandDetails(entry);
                ImGui::Separator();
            } else if (isHovered && commandHasDetails(entry)) {
                ImGui::BeginTooltip();
                printCommandDetails(entry);
                ImGui::EndTooltip();
            }

            // Misc stuff
            if (entry.type == 0 && entry.cmd() == 0xe1) {
                last_e1._reg = entry.args[0];
            } else if (entry.type == 0 && entry.cmd() == 0xe5) {
                last_offset_x = extend_sign<11>(entry.args[0] & 0x7ff);
                last_offset_y = extend_sign<11>((entry.args[0] >> 11) & 0x7ff);
            }
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    if (ImGui::Button("Save current dump")) {
        saveDumpDialog.callback
            = [sys](const gui::helper::File &f) { return GpuDrawList::save(sys, (f.entry.path() / (f.filename + f.extension)).string()); };
        saveDumpWindowOpen = true;
    }
    ImGui::SameLine();

    ImGui::Text("Frames to capture");
    ImGui::SameLine();
    ImGui::PushItemWidth(100.f);
    ImGui::InputInt("##Frames to capture", &framesToCapture);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Capture")) {
        GpuDrawList::framesToCapture = framesToCapture;
        sys->state = System::State::run;
    }

    ImGui::SameLine();
    if (ImGui::Button("Export textures")) {
        std::string exportPath = avocado::PATH_USER + "export/";
        try {
            fs::create_directory(exportPath);
        } catch (fs::filesystem_error &err) {
        }
        sys->gpu->vram = sys->gpu->prevVram;
        sys->gpu->gpuLogEnabled = false;

        auto &log = sys->gpu->gpuLogList;
        int i = 0;
        for (auto &entry : log) {
            if (entry.type == 0 && entry.cmd() == 0xe1) {
                last_e1._reg = entry.args[0];
            } else if (entry.type == 0 && entry.cmd() == 0xe5) {
                last_offset_x = extend_sign<11>(entry.args[0] & 0x7ff);
                last_offset_y = extend_sign<11>((entry.args[0] >> 11) & 0x7ff);
            }
            if (entry.cmd() >= 0x60 && entry.cmd() < 0x80) {
                auto &arguments = entry.args;
                gpu::RectangleArgs arg = entry.cmd();

                int16_t w = arg.getSize();
                int16_t h = arg.getSize();

                if (arg.size == 0) {
                    w = extend_sign<11>(arguments[(arg.isTextureMapped ? 3 : 2)] & 0xffff);
                    h = extend_sign<11>((arguments[(arg.isTextureMapped ? 3 : 2)] & 0xffff0000) >> 16);
                }

                int16_t x = extend_sign<11>(arguments[1] & 0xffff);
                int16_t y = extend_sign<11>((arguments[1] & 0xffff0000) >> 16);

                x += last_offset_x;
                y += last_offset_y;
                fmt::print("Entry {} texture mapped\n", i);

                if (arg.isTextureMapped && w > 0 && h > 0) {
                    int texX = arguments[2] & 0xff;
                    int texY = (arguments[2] & 0xff00) >> 8;
                    int clutX = ((arguments[2] >> 16) & 0x3f) * 16;
                    int clutY = ((arguments[2] >> 22) & 0x1ff);
                    int clutColors;
                    int textureWidth;
                    int textureBits;

                    ivec2 tex = ivec2(texX, texY);
                    ivec2 texPage = ivec2(last_e1.texturePageBaseX * 64, last_e1.texturePageBaseY * 256);

                    std::vector<uint8_t> dump;
                    dump.resize(w * h * 3);
                    for (int y = 0; y < h; y++) {
                        for (int x = 0; x < w; x++) {
                            PSXColor pixel;
                            if (last_e1.texturePageColors == gpu::GP0_E1::TexturePageColors::bit4) {
                                clutColors = 16;
                                textureWidth = w / 4;
                                textureBits = 4;
                                loadClutCacheIfRequired<ColorDepth::BIT_4>(sys->gpu.get(), ivec2(clutX, clutY));
                                pixel = tex4bit(sys->gpu.get(), tex + ivec2(x, y), texPage);
                            } else if (last_e1.texturePageColors == gpu::GP0_E1::TexturePageColors::bit8) {
                                clutColors = 256;
                                textureWidth = w / 2;
                                textureBits = 8;
                                loadClutCacheIfRequired<ColorDepth::BIT_8>(sys->gpu.get(), ivec2(clutX, clutY));
                                pixel = tex8bit(sys->gpu.get(), tex + ivec2(x, y), texPage);
                            } else {
                                clutColors = 0;
                                textureWidth = w;
                                textureBits = 16;
                                pixel = tex16bit(sys->gpu.get(), tex + ivec2(x, y), texPage);
                            }

                            dump[(y * w + x) * 3 + 0] = pixel.r << 3;  // r
                            dump[(y * w + x) * 3 + 1] = pixel.g << 3;  // g
                            dump[(y * w + x) * 3 + 2] = pixel.b << 3;  // b
                        }
                    }

                    stbi_write_png(fmt::format("{}{}.png", exportPath, i).c_str(), w, h, 3, dump.data(), w * 3);
                }
            }

            for (uint32_t arg : entry.args) {
                uint8_t addr = (entry.type == 0) ? 0 : 4;
                sys->gpu->write(addr, arg);
            }
            i++;
        }
        sys->gpu->gpuLogEnabled = true;
        toast(fmt::format("Exported textures to {}", exportPath));
    }

    ImGui::End();

    if (sys->state != System::State::run && renderTo >= 0) {
        GpuDrawList::replayCommands(sys->gpu.get(), renderTo);
    }
}

void GPU::vramWindow(gpu::GPU *gpu) {
    if (!vramImage) {
        vramImage = std::make_unique<Texture>(gpu::VRAM_WIDTH, gpu::VRAM_HEIGHT, GL_RGB, GL_RGB, GL_UNSIGNED_BYTE, false);
        vramUnpacked.resize(gpu::VRAM_WIDTH * gpu::VRAM_HEIGHT * 3);
    }

    // Update texture
    for (int y = 0; y < gpu::VRAM_HEIGHT; y++) {
        for (int x = 0; x < gpu::VRAM_WIDTH; x++) {
            PSXColor c = gpu->vram[y * gpu::VRAM_WIDTH + x];

            vramUnpacked[(y * gpu::VRAM_WIDTH + x) * 3 + 0] = c.r << 3;
            vramUnpacked[(y * gpu::VRAM_WIDTH + x) * 3 + 1] = c.g << 3;
            vramUnpacked[(y * gpu::VRAM_WIDTH + x) * 3 + 2] = c.b << 3;
        }
    }
    vramImage->update(vramUnpacked.data());

    blinkTimer += 0.0025 * M_PI;
    ImColor blinkColor = ImColor::HSV(blinkTimer, 1.f, 1.f, 0.75f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0, 0.0, 0.0, 1.0));

    auto defaultSize = ImVec2(1024, 512 + ImGui::GetFrameHeightWithSpacing() * 2);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(defaultSize.x / 2, defaultSize.y / 2), ImVec2(defaultSize.x * 2, defaultSize.y * 2),
        [](ImGuiSizeCallbackData *data) { data->DesiredSize.y = (data->DesiredSize.x / 2) + ImGui::GetFrameHeightWithSpacing() * 2; });

    ImGui::SetNextWindowSize(defaultSize);
    ImGui::Begin("VRAM", &vramWindowOpen, ImGuiWindowFlags_NoScrollbar);

    auto currentSize = ImGui::GetWindowContentRegionMax();
    currentSize.y = currentSize.x / 2;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(uintptr_t)vramImage->get(), currentSize);

    if (ImGui::Button("Original size")) {
        ImGui::SetWindowSize(defaultSize);
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    float scale = currentSize.x / defaultSize.x;

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    for (auto area : vramAreas) {
        ImVec2 a, b;
        a.x = cursorPos.x + area.pos.x * scale;
        a.y = cursorPos.y + area.pos.y * scale;

        b.x = cursorPos.x + (area.pos.x + area.size.x) * scale;
        b.y = cursorPos.y + (area.pos.y + area.size.y) * scale;

        drawList->AddRectFilled(a, b, blinkColor, 0.f, 0);

        if (ImGui::IsMouseHoveringRect(a, b)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(area.name.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}

void GPU::displayWindows(System *sys) {
    if (registersWindowOpen) registersWindow(sys);
    if (logWindowOpen) logWindow(sys);
    if (vramWindowOpen) vramWindow(sys->gpu.get());
    if (saveDumpWindowOpen) saveDumpDialog.display(saveDumpWindowOpen);
}
};  // namespace gui::debug