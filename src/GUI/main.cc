#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "ArxivGuiApp.hh"
#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"
#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Fetcher.hh"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <memory>
#include <string>

static void glfw_error_callback(int error, const char *description) {
    spdlog::error("GLFW error {}: {}", error, description);
}

int main(int /*argc*/, char ** /*argv*/) {
    try {
        auto logger = spdlog::basic_logger_mt("arxiv_gui", "arxiv_gui.log");
        spdlog::set_default_logger(logger);
    } catch (...) {}
    spdlog::set_level(spdlog::level::info);

    Arxiv::Config config;
    try {
        config = Arxiv::Config(".arxiv-tui.yml");
    } catch (...) {
        spdlog::warn("Config file not found, using defaults");
    }

    auto db      = std::make_unique<Arxiv::DatabaseManager>("articles.db");
    auto fetcher = std::make_unique<Arxiv::Fetcher>(config.get_topics(),
                                                     config.get_download_dir());
    Arxiv::AppCore core(config, std::move(db), std::move(fetcher),
                        Arxiv::AppCore::FetchMode::Async);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        spdlog::critical("Failed to initialise GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(1280, 800, "arXiv Browser", nullptr, nullptr);
    if (!window) {
        spdlog::critical("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ArxivGuiApp app(core, [window]() {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.render();

        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
