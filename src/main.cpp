#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

// ---- App state ----

static ma_engine        g_audio_engine;
static ma_sound         g_sound;
static bool             g_sound_inited = false;
static std::string      g_current_file;
static std::string      g_last_folder;
static bool             g_playing = false;
static bool             g_loop = false;
static float            g_volume = 1.0f;
static bool             g_quit = false;

// ---- FX chain state ----

struct FxInstance
{
    std::string type;
    std::string label;
    bool        window_open = true;

    // Break Beat params
    struct {
        float speed     = 1.0f;
        float intensity = 0.5f;
        int   pattern   = 0;
    } bb;
};

static std::vector<FxInstance> g_fx_chain;
static int g_next_bb_id = 1;
static bool g_show_fx_chain = true;

static void add_fx_instance(const char* type)
{
    FxInstance inst;
    inst.type = type;
    if (strcmp(type, "Break Beat") == 0)
    {
        inst.label = "Break Beat " + std::to_string(g_next_bb_id++);
        inst.window_open = true;
    }
    g_fx_chain.push_back(inst);
}

// ---- File browser state ----

static bool             g_show_browser = false;
static std::string      g_browser_folder;
static std::vector<fs::directory_entry> g_browser_entries;
static int              g_browser_sel = -1;

// ---- Audio helpers ----

static void stop_audio()
{
    if (g_sound_inited)
    {
        ma_sound_stop(&g_sound);
        ma_sound_uninit(&g_sound);
        g_sound_inited = false;
    }
    g_playing = false;
}

static bool play_audio(const char* path)
{
    stop_audio();

    ma_result res = ma_sound_init_from_file(&g_audio_engine, path, 0, NULL, NULL, &g_sound);
    if (res != MA_SUCCESS)
        return false;

    g_sound_inited = true;
    ma_sound_set_volume(&g_sound, g_volume);
    ma_sound_set_looping(&g_sound, g_loop);
    ma_sound_start(&g_sound);
    g_playing = true;
    return true;
}

static void update_playing_state()
{
    if (g_sound_inited && !ma_sound_is_playing(&g_sound))
        stop_audio();
}

// ---- File browser helpers ----

static void refresh_browser_entries()
{
    g_browser_entries.clear();
    g_browser_sel = -1;

    try
    {
        for (auto& e : fs::directory_iterator(g_browser_folder))
            g_browser_entries.push_back(e);
    }
    catch (...) {}

    std::sort(g_browser_entries.begin(), g_browser_entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            bool a_dir = a.is_directory();
            bool b_dir = b.is_directory();
            if (a_dir != b_dir) return a_dir;
            std::string an = a.path().filename().string();
            std::string bn = b.path().filename().string();
            std::transform(an.begin(), an.end(), an.begin(), ::tolower);
            std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
            return an < bn;
        });
}

static bool is_wav_ext(const fs::path& p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".wav";
}

// ---- File browser window ----

static void draw_file_browser(bool* open)
{
    if (!*open) return;

    ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Open Audio File", open, ImGuiWindowFlags_NoDocking))
    {
        ImGui::End();
        return;
    }

    char buf[1024];
    strncpy(buf, g_browser_folder.c_str(), sizeof(buf) - 1);

    float iw = ImGui::GetContentRegionAvail().x - 80.0f;
    ImGui::SetNextItemWidth(iw);
    if (ImGui::InputText("##folder", buf, sizeof(buf)))
        g_browser_folder = buf;
    ImGui::SameLine();
    if (ImGui::Button("Go"))
    {
        if (fs::exists(g_browser_folder) && fs::is_directory(g_browser_folder))
            refresh_browser_entries();
    }

    ImGui::BeginChild("##files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8), true);

    if (g_browser_folder != "/")
    {
        bool sel = false;
        if (ImGui::Selectable("[..]", &sel, ImGuiSelectableFlags_AllowDoubleClick))
            if (ImGui::IsMouseDoubleClicked(0))
            {
                g_browser_folder = fs::path(g_browser_folder).parent_path().string();
                refresh_browser_entries();
            }
    }

    for (int i = 0; i < (int)g_browser_entries.size(); i++)
    {
        const auto& e = g_browser_entries[i];
        bool is_dir = e.is_directory();
        if (!is_dir && !is_wav_ext(e.path()))
            continue;

        std::string label;
        if (is_dir)
            label = "[ " + e.path().filename().string() + " ]";
        else
            label = e.path().filename().string();

        bool is_sel = (i == g_browser_sel);
        if (ImGui::Selectable(label.c_str(), &is_sel, ImGuiSelectableFlags_AllowDoubleClick))
        {
            g_browser_sel = i;
            if (ImGui::IsMouseDoubleClicked(0))
            {
                if (is_dir)
                {
                    g_browser_folder = e.path().string();
                    refresh_browser_entries();
                }
                else
                {
                    g_current_file = e.path().string();
                    g_last_folder = g_browser_folder;
                    *open = false;
                }
            }
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginDisabled(g_browser_sel < 0 ||
        g_browser_sel >= (int)g_browser_entries.size() ||
        g_browser_entries[g_browser_sel].is_directory());
    if (ImGui::Button("Open", ImVec2(120, 0)))
    {
        g_current_file = g_browser_entries[g_browser_sel].path().string();
        g_last_folder = g_browser_folder;
        *open = false;
    }
    ImGui::EndDisabled();
    ImGui::End();
}

// ---- Settings persistence (last folder) ----

static void* settings_read_open(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    return (strcmp(name, "AudioPlayer") == 0) ? (void*)1 : nullptr;
}

static void settings_read_line(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    char folder[4096];
    if (sscanf(line, "LastFolder=%4095[^\n]", folder) == 1)
        g_last_folder = folder;
}

static void settings_write_all(ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][AudioPlayer]\n", h->TypeName);
    buf->appendf("LastFolder=%s\n", g_last_folder.c_str());
}

static void install_settings_handler()
{
    ImGuiSettingsHandler h;
    h.TypeName = "Audio";
    h.TypeHash = ImHashStr("Audio");
    h.ReadOpenFn = settings_read_open;
    h.ReadLineFn = settings_read_line;
    h.WriteAllFn = settings_write_all;
    ImGui::GetCurrentContext()->SettingsHandlers.push_back(h);
}

// ---- Main window ----

static void draw_audio_player()
{
    ImGui::Begin("Audio Player", nullptr, ImGuiWindowFlags_NoDocking);

    if (!g_current_file.empty())
    {
        fs::path p(g_current_file);
        ImGui::Text("File: %s", p.filename().string().c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", g_current_file.c_str());
    }
    else
    {
        ImGui::Text("No file loaded");
    }

    ImGui::SameLine();
    if (ImGui::Button("Browse"))
    {
        if (g_browser_folder.empty())
        {
            if (!g_last_folder.empty())
                g_browser_folder = g_last_folder;
            else
                g_browser_folder = fs::current_path().string();
        }
        refresh_browser_entries();
        g_show_browser = true;
    }

    // Play / Stop
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BeginDisabled(g_current_file.empty());
    if (g_playing)
    {
        if (ImGui::Button("Stop", ImVec2(100, 0)))
            stop_audio();
    }
    else
    {
        if (ImGui::Button("Play", ImVec2(100, 0)))
        {
            if (!play_audio(g_current_file.c_str()))
                fprintf(stderr, "Failed to play: %s\n", g_current_file.c_str());
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (g_playing)
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Playing");
    else
        ImGui::Text("Stopped");

    // Volume
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Text("Volume");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("##vol", &g_volume, 0.0f, 1.0f, "%.2f"))
        if (g_sound_inited)
            ma_sound_set_volume(&g_sound, g_volume);

    // Loop
    if (ImGui::Checkbox("Loop", &g_loop))
        if (g_sound_inited)
            ma_sound_set_looping(&g_sound, g_loop);

    // Process
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Separator();
    ImGui::BeginDisabled(g_fx_chain.empty());
    if (ImGui::Button("Process", ImVec2(120, 0)))
    {
        // TODO: implement processing logic
    }
    ImGui::EndDisabled();
    ImGui::Text("Application avg %.3f ms/frame (%.1f FPS)",
        1000.0 / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::End();
}

// ---- FX Chain window ----

static void draw_fx_chain(bool* open)
{
    if (!*open) return;

    ImGui::SetNextWindowSize(ImVec2(320, 260), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("FX Chain", open, ImGuiWindowFlags_NoDocking))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginChild("##fxlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), true))
    {
        for (int i = 0; i < (int)g_fx_chain.size(); i++)
        {
            // Move up
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton((std::to_string(i) + "u").c_str(), ImGuiDir_Up))
                std::swap(g_fx_chain[i], g_fx_chain[i - 1]);
            ImGui::EndDisabled();
            ImGui::SameLine();

            // Move down
            ImGui::BeginDisabled(i == (int)g_fx_chain.size() - 1);
            if (ImGui::ArrowButton((std::to_string(i) + "d").c_str(), ImGuiDir_Down))
                std::swap(g_fx_chain[i], g_fx_chain[i + 1]);
            ImGui::EndDisabled();
            ImGui::SameLine();

            // Remove
            if (ImGui::SmallButton((std::string("X##") + std::to_string(i)).c_str()))
            {
                g_fx_chain.erase(g_fx_chain.begin() + i);
                i--;
                continue;
            }
            ImGui::SameLine();

            // Click to focus that effect's window
            if (ImGui::Selectable(g_fx_chain[i].label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                ImGui::SetWindowFocus(g_fx_chain[i].label.c_str());
        }
    }
    ImGui::EndChild();

    // Add effect
    ImGui::Separator();
    static int add_type = 0;
    const char* items[] = { "Break Beat" };
    ImGui::Combo("##addfx", &add_type, items, IM_ARRAYSIZE(items));
    ImGui::SameLine();
    if (ImGui::Button("Add"))
        add_fx_instance(items[add_type]);

    ImGui::End();
}

// ---- Break Beat window ----

static void draw_break_beat(FxInstance& inst)
{
    if (!inst.window_open) return;

    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(inst.label.c_str(), &inst.window_open, ImGuiWindowFlags_NoDocking))
    {
        ImGui::End();
        return;
    }

    ImGui::SliderFloat("Speed", &inst.bb.speed, 0.25f, 4.0f, "%.2fx");
    ImGui::SliderFloat("Intensity", &inst.bb.intensity, 0.0f, 1.0f, "%.2f");
    ImGui::Combo("Pattern", &inst.bb.pattern, "Half-Time\0Double-Time\0Random-Gate\0Stutter\0\0");

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Configure how the break-beat effect\nmanipulates the audio.");

    ImGui::End();
}

// ---- Error callback ----

static void glfw_error_callback(int error, const char* desc)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}

// ---- Entry point ----

int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

#if defined(IMGUI_IMPL_OPENGL_ES2)
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow(
        (int)(1280 * main_scale), (int)(800 * main_scale),
        "Glitcher", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    install_settings_handler();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Init audio engine
    if (ma_engine_init(NULL, &g_audio_engine) != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize audio engine\n");
        return 1;
    }

    ImVec4 clear_color = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);

#ifdef __EMSCRIPTEN__
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!glfwWindowShouldClose(window))
#endif
    {
        glfwPollEvents();

        if (g_quit || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)))
            glfwSetWindowShouldClose(window, true);

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Dockspace host window with menu bar
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGuiWindowFlags host_flags =
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

        ImGui::Begin("DockSpace", nullptr, host_flags);
        ImGui::PopStyleVar(3);

        // Menu bar
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                    g_quit = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("FX"))
            {
                if (ImGui::MenuItem("Break Beat"))
                    add_fx_instance("Break Beat");
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::DockSpace(ImGui::GetID("DockSpace"));

        update_playing_state();
        draw_audio_player();
        draw_file_browser(&g_show_browser);
        draw_fx_chain(&g_show_fx_chain);

        for (auto& inst : g_fx_chain)
        {
            if (inst.type == "Break Beat")
                draw_break_beat(inst);
        }

        // Remove closed instances
        for (int i = (int)g_fx_chain.size() - 1; i >= 0; i--)
            if (!g_fx_chain[i].window_open)
                g_fx_chain.erase(g_fx_chain.begin() + i);

        ImGui::End();

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(clear_color.x * clear_color.w,
                     clear_color.y * clear_color.w,
                     clear_color.z * clear_color.w,
                     clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    stop_audio();
    ma_engine_uninit(&g_audio_engine);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
