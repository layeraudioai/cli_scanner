// project_creator.cpp
// Implements creation of a multi‑language project skeleton based on an NLP prompt.
// The generated layout:
//   <base_dir>/<project_name>/
//       cpp/      – minimal C++ "main.cpp" and Makefile
//       csharp/   – minimal C# Program.cs and .csproj placeholder
//       python/   – minimal Python main.py
// The original prompt text is inserted as a comment at the top of each generated file.

#include "project_creator.h"
#include <fstream>
#include <iostream>
#include <string>
#include <regex>
#include <filesystem>
#include <vector>
#include <sstream>

namespace fs = std::filesystem;

// Utility: sanitize a string to be a safe directory name (alphanumerics and underscores)
static std::string sanitize_name(const std::string &input) {
    std::string out;
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += c;
        } else if (c == ' ' || c == '-' || c == '_') {
            out += '_';
        }
        // ignore other characters
    }
    if (out.empty()) out = "project";
    return out;
}

static void write_file(const fs::path &filepath, const std::string &content) {
    std::ofstream ofs(filepath);
    if (!ofs) {
        std::cerr << "Failed to write " << filepath << "\n";
        return;
    }
    ofs << content;
    ofs.close();
}

static std::string format_comment(const std::string &prompt, const std::string &prefix) {
    std::stringstream ss(prompt);
    std::string segment;
    std::string result;
    while (std::getline(ss, segment, '.')) {
        if (!segment.empty()) {
            result += prefix + segment + "\n";
        }
    }
    return result;
}

void create_project_from_prompt(const std::string &prompt) {
    std::string proj_name = sanitize_name(prompt);
    
    // Detect desired project type from prompt
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    std::string lower_prompt = to_lower(prompt);
    
    enum class ProjectKind { Game, Gui, Web, Console, Script };
    ProjectKind kind = ProjectKind::Script;
    if (lower_prompt.find("game") != std::string::npos || lower_prompt.find("play") != std::string::npos) kind = ProjectKind::Game;
    else if (lower_prompt.find("gui") != std::string::npos || lower_prompt.find("window") != std::string::npos) kind = ProjectKind::Gui;
    else if (lower_prompt.find("web") != std::string::npos || lower_prompt.find("http") != std::string::npos) kind = ProjectKind::Web;
    else if (lower_prompt.find("cli") != std::string::npos || lower_prompt.find("console") != std::string::npos) kind = ProjectKind::Console;

    // Additional framework detection flags
    bool use_monogame = lower_prompt.find("monogame") != std::string::npos || lower_prompt.find("xna") != std::string::npos;
    bool use_unity = lower_prompt.find("unity") != std::string::npos || lower_prompt.find("3d") != std::string::npos;
    bool use_gbastudio = lower_prompt.find("gba") != std::string::npos || lower_prompt.find("gbstudio") != std::string::npos || lower_prompt.find("gb studio") != std::string::npos || (lower_prompt.find("gameboy") != std::string::npos && lower_prompt.find("gameboyadvance") == std::string::npos && lower_prompt.find("gba") == std::string::npos) || lower_prompt.find("sgb") != std::string::npos || lower_prompt.find("sgbp") != std::string::npos || lower_prompt.find("sgbc") != std::string::npos;

    fs::path base_dir = fs::current_path() / "create_projects" / proj_name;
    try {
        fs::create_directories(base_dir / "cpp");
        fs::create_directories(base_dir / "csharp");
        fs::create_directories(base_dir / "python");
    } catch (const fs::filesystem_error &e) {
        std::cerr << "Directory creation error: " << e.what() << "\n";
        return;
    }

    // C++ skeleton generation
    std::string cpp_main;
    switch (kind) {
        case ProjectKind::Game: {
            if (use_gbastudio) {
                // Copy GB Studio sample project files
                fs::path sample_dir = fs::current_path() / "gbstudio_sample" / "gbstudio";
                if (fs::exists(sample_dir)) {
                    // Copy all sample files to the project cpp directory
                    fs::copy(sample_dir, base_dir / "cpp", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                    // Check for custom modification block in the prompt
                    std::regex mod_regex(R"((?s)\[GBSTUDIO_MOD\](.*?)\[/GBSTUDIO_MOD\])");
                    std::smatch match;
                    if (std::regex_search(prompt, match, mod_regex)) {
                        // Write the custom C source provided by the user into main.cpp
                        std::string custom_source = match.str(1);
                        write_file(base_dir / "cpp" / "main.cpp", custom_source);
                    }
                } else {
                    // fallback minimal placeholder
                    cpp_main = format_comment(prompt, "//") +
                        "#include <gb.h> // Placeholder\n"
                        "#include <iostream>\n\n"
                        "int main() {\n"
                        "    std::cout << \"Hello from GB project: " + proj_name + "\" << std::endl;\n"
                        "    return 0;\n"
                        "}\n";
                }
            } else {
                cpp_main = format_comment(prompt, "//") +
                    "#include <SDL.h>\n"
                    "#include <iostream>\n\n"
                    "int main(int argc, char* argv[]) {\n"
                    "    if (SDL_Init(SDL_INIT_VIDEO) != 0) {\n"
                    "        std::cerr << \"SDL_Init Error: \" << SDL_GetError() << std::endl;\n"
                    "        return 1;\n"
                    "    }\n"
                    "    SDL_Window* win = SDL_CreateWindow(\"" + proj_name + "\", 100, 100, 800, 600, SDL_WINDOW_SHOWN);\n"
                    "    if (!win) {\n"
                    "        std::cerr << \"SDL_CreateWindow Error: \" << SDL_GetError() << std::endl;\n"
                    "        SDL_Quit();\n"
                    "        return 1;\n"
                    "    }\n"
                    "    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);\n"
                    "    bool running = true;\n"
                    "    while (running) {\n"
                    "        SDL_Event e;\n"
                    "        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = false;\n"
                    "        SDL_SetRenderDrawColor(renderer, 0,0,0,255);\n"
                    "        SDL_RenderClear(renderer);\n"
                    "        // TODO: rendering\n"
                    "        SDL_RenderPresent(renderer);\n"
                    "        SDL_Delay(16);\n"
                    "    }\n"
                    "    SDL_DestroyRenderer(renderer);\n"
                    "    SDL_DestroyWindow(win);\n"
                    "    SDL_Quit();\n"
                    "    return 0;\n"
                    "}\n";
            }
            break;
        }
        case ProjectKind::Gui: {
            cpp_main = format_comment(prompt, "//") +
                "#include <QApplication>\n"
                "#include <QWidget>\n\n"
                "int main(int argc, char *argv[]) {\n"
                "    QApplication app(argc, argv);\n"
                "    QWidget window;\n"
                "    window.resize(800, 600);\n"
                "    window.setWindowTitle(\"" + proj_name + "\");\n"
                "    window.show();\n"
                "    return app.exec();\n"
                "}\n";
            break;
        }
        case ProjectKind::Web: {
            cpp_main = format_comment(prompt, "//") +
                "#include <iostream>\n"
                "#include <cpp-httplib/httplib.h>\n\n"
                "int main() {\n"
                "    httplib::Server svr;\n"
                "    svr.Get(\"/\", [&](const httplib::Request&, httplib::Response& res) {\n"
                "        res.set_content(\"Hello from " + proj_name + "\", \"text/plain\");\n"
                "    });\n"
                "    svr.listen(\"0.0.0.0\", 8080);\n"
                "    return 0;\n"
                "}\n";
            break;
        }
        default: {
            cpp_main = format_comment(prompt, "//") +
                "#include <iostream>\n\n"
                "int main() {\n"
                "    std::cout << \"Hello from " + proj_name + "!\" << std::endl;\n"
                "    return 0;\n"
                "}\n";
        }
    }
    write_file(base_dir / "cpp" / "main.cpp", cpp_main);

    std::string cpp_make = "# Simple Makefile for " + proj_name + " (C++)\n"
        "CXX = g++\n"
        "CXXFLAGS = -Wall -Wextra -O2\n"
        "LDFLAGS = -lSDL2\n"
        "TARGET = " + proj_name + "\n"
        "all: $(TARGET)\n\n"
        "$(TARGET): main.cpp\n"
        "\t$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp $(LDFLAGS)\n\n"
        "clean:\n"
        "\trm -f $(TARGET)\n";
    write_file(base_dir / "cpp" / "Makefile", cpp_make);

    // C# skeleton generation based on detected kind and framework flags
    std::string cs_prog;
    if (kind == ProjectKind::Game) {
        if (use_monogame) {
            cs_prog = format_comment(prompt, "//") +
                "using System;\n"
                "using Microsoft.Xna.Framework;\n"
                "using Microsoft.Xna.Framework.Graphics;\n"
                "using Microsoft.Xna.Framework.Input;\n\n"
                "public class Game1 : Game {\n"
                "    GraphicsDeviceManager graphics;\n"
                "    SpriteBatch spriteBatch;\n\n"
                "    public Game1() {\n"
                "        graphics = new GraphicsDeviceManager(this);\n"
                "        Content.RootDirectory = \"Content\";\n"
                "    }\n\n"
                "    protected override void Initialize() {\n"
                "        base.Initialize();\n"
                "    }\n\n"
                "    protected override void LoadContent() {\n"
                "        spriteBatch = new SpriteBatch(GraphicsDevice);\n"
                "        // TODO: Load assets\n"
                "    }\n\n"
                "    protected override void Update(GameTime gameTime) {\n"
                "        if (GamePad.GetState(PlayerIndex.One).Buttons.Back == ButtonState.Pressed) Exit();\n"
                "        base.Update(gameTime);\n"
                "    }\n\n"
                "    protected override void Draw(GameTime gameTime) {\n"
                "        GraphicsDevice.Clear(Color.CornflowerBlue);\n"
                "        // TODO: Draw game\n"
                "        base.Draw(gameTime);\n"
                "    }\n"
                "}\n\n"
                "public static class Program {\n"
                "    [STAThread]\n"
                "    static void Main() {\n"
                "        using (var game = new Game1()) { game.Run(); }\n"
                "    }\n"
                "}\n";
        } else if (use_unity) {
            cs_prog = format_comment(prompt, "//") +
                "using UnityEngine;\n\n"
                "public class " + proj_name + "Behaviour : MonoBehaviour {\n"
                "    void Start() {\n"
                "        Debug.Log(\"Hello from Unity project: " + proj_name + "\");\n"
                "    }\n"
                "    void Update() {\n"
                "        // TODO: Game logic\n"
                "    }\n"
                "}\n";
        } else {
            cs_prog = format_comment(prompt, "//") +
                "using System;\n"
                "using SDL2; // Placeholder – you need SDL2# bindings\n\n"
                "class Program {\n"
                "    static void Main(string[] args) {\n"
                "        SDL.SDL_Init(SDL.SDL_INIT_VIDEO);\n"
                "        var window = SDL.SDL_CreateWindow(\"" + proj_name + "\",\n"
                "            SDL.SDL_WINDOWPOS_CENTERED,\n"
                "            SDL.SDL_WINDOWPOS_CENTERED,\n"
                "            800, 600,\n"
                "            SDL.SDL_WindowFlags.SDL_WINDOW_SHOWN);\n"
                "        var renderer = SDL.SDL_CreateRenderer(window, -1, SDL.SDL_RendererFlags.SDL_RENDERER_ACCELERATED);\n"
                "        bool running = true;\n"
                "        while (running) {\n"
                "            while (SDL.SDL_PollEvent(out var e) == 1) {\n"
                "                if (e.type == SDL.SDL_EventType.SDL_QUIT) running = false;\n"
                "            }\n"
                "            SDL.SDL_SetRenderDrawColor(renderer, 0,0,0,255); SDL.SDL_RenderClear(renderer);\n"
                "            SDL.SDL_SetRenderDrawColor(renderer, 255,255,255,255);\n"
                "            SDL.SDL_RenderPresent(renderer); SDL.SDL_Delay(16);\n"
                "        }\n"
                "        SDL.SDL_DestroyRenderer(renderer); SDL.SDL_DestroyWindow(window); SDL.SDL_Quit();\n"
                "        Console.WriteLine(\"Game placeholder for " + proj_name + "\");\n"
                "    }\n"
                "}\n";
        }
    } else {
        cs_prog = format_comment(prompt, "//") +
            "using System;\n\n"
            "class Program {\n"
            "    static void Main(string[] args) {\n"
            "        Console.WriteLine(\"Hello from " + proj_name + "!\");\n"
            "    }\n"
            "}\n";
    }
    write_file(base_dir / "csharp" / "Program.cs", cs_prog);

    // simple csproj placeholder
    std::string csproj = "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
        "  <PropertyGroup>\n"
        "    <OutputType>Exe</OutputType>\n"
        "    <TargetFramework>net6.0</TargetFramework>\n"
        "    <RootNamespace>" + proj_name + "</RootNamespace>\n"
        "  </PropertyGroup>\n"
        "  <ItemGroup>\n"
        "    <PackageReference Include=\"SDL2-CS\" Version=\"2.0.30\" />\n"
        "  </ItemGroup>\n"
        "</Project>\n";
    write_file(base_dir / "csharp" / (proj_name + ".csproj"), csproj);

    // Python skeleton generation based on detected kind
    std::string py_main;
    if (kind == ProjectKind::Game) {
        py_main = format_comment(prompt, "#") +
            "import pygame\n"
            "def main():\n"
            "    pygame.init()\n"
            "    screen = pygame.display.set_mode((800, 600))\n"
            "    pygame.display.set_caption('" + proj_name + "')\n"
            "    clock = pygame.time.Clock()\n"
            "    paddle = pygame.Rect(350, 560, 100, 20)\n"
            "    ball = pygame.Rect(390, 300, 20, 20)\n"
            "    ball_vel_x, ball_vel_y = 4, -4\n"
            "    running = True\n"
            "    while running:\n"
            "        for event in pygame.event.get():\n"
            "            if event.type == pygame.QUIT:\n"
            "                running = False\n"
            "        keys = pygame.key.get_pressed()\n"
            "        if keys[pygame.K_a] and paddle.x > 0:\n"
            "            paddle.x -= 6\n"
            "        if keys[pygame.K_d] and paddle.x + paddle.width < 800:\n"
            "            paddle.x += 6\n"
            "        ball.x += ball_vel_x\n"
            "        ball.y += ball_vel_y\n"
            "        if ball.x <= 0 or ball.x + ball.width >= 800:\n"
            "            ball_vel_x = -ball_vel_x\n"
            "        if ball.y <= 0:\n"
            "            ball_vel_y = -ball_vel_y\n"
            "        if ball.colliderect(paddle):\n"
            "            ball_vel_y = -abs(ball_vel_y)\n"
            "        screen.fill((0,0,0))\n"
            "        pygame.draw.rect(screen, (255,255,255), paddle)\n"
            "        pygame.draw.ellipse(screen, (255,255,255), ball)\n"
            "        pygame.display.flip()\n"
            "        clock.tick(60)\n"
            "    pygame.quit()\n"
            "if __name__ == '__main__':\n"
            "    main()\n";
    } else {
        py_main = format_comment(prompt, "#") +
            "def main():\n"
            "    print('Hello from " + proj_name + "!')\n\n"
            "if __name__ == '__main__':\n"
            "    main()\n";
    }
    write_file(base_dir / "python" / "main.py", py_main);

    std::cout << "Created multi‑language project at: " << base_dir << "\n";
}
