#include <memory>
#include <cstdlib>
#include <string>
#include <vector>
#include <regex>

#include <cxxopts.hpp>

#include "World.h"
#include "GameManager.h"
#include "Startup.h"

#include "Rendering/Font.h"
#include "Parsers/MlbFile.h"

#include "System/System.h"
#include "System/Logger.h"
#include "System/FileUtil.h"

#include "GUI/NarrScreen.h"
#include "GUI/MainMenu.h"

using namespace Sourcehold;
using namespace Game;
using namespace Audio;
using namespace System;
using namespace Assets;
using namespace Parsers;
using namespace Rendering;
using namespace GUI;

void Cleanup()
{
    SaveConfig();
    UnloadFonts();
    ClearFileCache();
    DestroyManager();
}

int MainLoop(UIState state) {
    switch(state) {
    case MAIN_MENU: {
        MainMenu *menu = new MainMenu();
        state = menu->EnterMenu();
        delete menu;

        MainLoop(state);
    } break;
    case MILITARY_CAMPAIGN_MISSION: {
        int index = 0; // todo

        NarrScreen *narr = new NarrScreen(index + 1);
        narr->Begin();
        delete narr;

        // TODO
        Song music(GetDirectory() / "fx/music/the maidenA.raw", true);
        music.Play();

        World *world = new World();
        world->LoadFromDisk(GetDirectory() / "maps/mission1.map");
        state = world->Play();

        music.Stop();

        MainLoop(state);
    } break;
    case EXIT_GAME: break;
    default: break;
    }

    Cleanup();
    return EXIT_SUCCESS;
}

int EnterLoadingScreen()
{
    std::shared_ptr<TgxFile> tgx_loading = GetTgx("gfx/frontend_loading.tgx");

    /* Get the assets */
    std::vector<ghc::filesystem::path> files = GetDirectoryRecursive(GetDirectory(), ".ani");
    if(files.empty()) {
        return EXIT_FAILURE;
    }

    /* Preload some assets */
    uint32_t index = 0;

    /* Calculater the position */
    Size loadingBgSz = { 1024, 768 };
    int px = (Rendering::GetWidth() / 2) - (loadingBgSz.w / 2);
    int py = (GetHeight() / 2) - (loadingBgSz.h / 2);

    ResetTarget();

    Resolution res = GetResolution();
    StrongholdEdition ed = GetEdition();

    while(Running() && index < files.size()-1) {
        ClearDisplay();

#if RENDER_LOADING_BORDER == 1
        if(ed == STRONGHOLD_HD && res != RESOLUTION_800x600) {
            RenderMenuBorder();
        }
#endif

        /* Load a file */
        ghc::filesystem::path path = files.at(index);
        Cache(path);
        index++;

        /* Normalized loading progess */
        double progress = (double)index / (double)files.size();

        /* Render the background */
        Render(*tgx_loading, px, py);

        /* Render the loading bar */
        Size barSz = { 450, 35 };
        int padding = 10;
        RenderRect(Rect<int>{ px+(loadingBgSz.w/2)-(barSz.w/2), py+int(loadingBgSz.h/1.3), barSz.w, barSz.h }, 0, 0, 0, 128, true);
        RenderRect(Rect<int>{ px+(loadingBgSz.w/2)-(barSz.w/2), py+int(loadingBgSz.h/1.3), barSz.w, barSz.h }, 0, 0, 0, 255, false);
        RenderRect(Rect<int>{ px+5+(loadingBgSz.w/2)-(barSz.w/2), py+5+int(loadingBgSz.h/1.3), int((barSz.w-padding)*progress), barSz.h-padding }, 0, 0, 0, 255, true);

        FlushDisplay();

        SDL_Delay(1);
    }

    return Running() ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const int resolutions[][2] = {
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1280, 1024 },
    { 1366, 768 },
    { 1440, 900 },
    { 1600, 900 },
    { 1600, 1200 },
    { 1680, 1050 },
    { 1920, 1080 },
    { 2560, 1600 }
};

int StartGame(GameOptions& opt)
{
    // Init logger //
#if SOURCEHOLD_UNIX == 1
    Logger::SetColorOutput(true);
#else
    Logger::SetColorOutput(false);
#endif

    if (opt.color >= 0) Logger::SetColorOutput(opt.color == 1);

    Resolution res;
    // Convert resolution //
    bool found = false;
    for (int i = 0; i < (sizeof(resolutions) / sizeof(int)) / 2; i++) {
        int w = resolutions[i][0];
        int h = resolutions[i][1];

        if (w == opt.width && h == opt.height) {
            res = static_cast<Resolution>(i);
            found = true;
            break;
        }
    }

    if (!found) {
        opt.width = 800; opt.height = 600;
        res = RESOLUTION_800x600;
    }

    // Init game //
    Logger::message(GAME) << "Starting version " SOURCEHOLD_VERSION_STRING " (" SOURCEHOLD_BUILD ")" << std::endl;

    if(!InitManager(opt, res) || !LoadGameData()) {
        ErrorMessageBox(
            "Here's a Nickel, kid. Go buy yourself a real Stronghold",
            std::string("Please make sure the data directory contains all necessary files.\n") +
            + "Current path: " +
            GetDirectory().string()
        );
        return EXIT_FAILURE;
    }
    if(!LoadFonts()) {
        Logger::error(GAME) << "Error while loading fonts!" << std::endl;
        return EXIT_FAILURE;
    }

    Logger::message(GAME) << "Done" << std::endl;

    Startup *start = new Startup();

    int ret = EnterLoadingScreen();
    if (ret != EXIT_SUCCESS) return ret;

    UIState state = MAIN_MENU;
    if(!opt.skip) {
        start->PlayMusic();
        state = start->Begin();
    }
    
    delete start;
    return MainLoop(state);
}

#undef main

/* Common entry point across all platforms */
int main(int argc, char **argv)
{
    namespace po = cxxopts;
    namespace fs = ghc::filesystem;

    // Parse commandline //
    try {
        GameOptions opt;

        po::Options config("Sourcehold", "Open-source Stronghold");

        config.add_options()
            ("h,help", "Print this info")
            ("v,version", "Print version string")
            ("resolutions", "List available resolutions and exit")
            ("p,path", "Custom path to data folder", po::value<std::string>()->default_value("../data/"))
            ("debug", "Print debug info", po::value<bool>(opt.debug))
            ("c,color", "Force color output")
            ("f,fullscreen", "Run in fullscreen mode", po::value<bool>(opt.fullscreen))
            ("r,resolution", "Resolution of the window", po::value<std::string>()->default_value("1280x720"))
            ("d,disp", "Index of the monitor to be used", po::value<uint16_t>()->default_value("0"))
            ("s,skip", "Skip directly to the main menu", po::value<bool>(opt.skip))
            ("noborder", "Remove window border", po::value<bool>(opt.noborder))
            ("nograb", "Don't grab the mouse", po::value<bool>(opt.nograb))
            ("nosound", "Disable sound entirely", po::value<bool>(opt.nosound))
            ("nothread", "Disable threading", po::value<bool>(opt.nothread))
            ("nocache", "Disable asset caching", po::value<bool>(opt.nocache));

        auto result = config.parse(argc, argv);
        if (result["help"].as<bool>()) {
            std::cout << config.help() << std::endl;
            return EXIT_SUCCESS;
        }

        if (result["resolutions"].as<bool>()) {
            for (int i = 0; i < (sizeof(resolutions) / sizeof(int)) / 2; i++) {
                std::cout << resolutions[i][0] << "x" << resolutions[i][1] << std::endl;
            }
            return EXIT_SUCCESS;
        }

        if (result.count("color") > 0) opt.color = result["color"].as<bool>();
        else opt.color = -1;

        opt.ndisp = result["disp"].as<uint16_t>();
        opt.dataDir = result["path"].as<std::string>();

        std::regex regex("(\\d+)x(\\d+)");
        std::smatch match;

        const std::string str = result["resolution"].as<std::string>();
        if (std::regex_search(str.begin(), str.end(), match, regex)) {
            opt.width  = std::stoi(match[1]);
            opt.height = std::stoi(match[2]);
        }
        else {
            // fallback
            opt.width = 800; opt.height = 600;
        }

        return StartGame(opt);
    }
    catch (po::OptionException & e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

#if SOURCEHOLD_MINGW == 1 && 0

#include <windows.h>
#include <string>
#include <vector>

/* Windows specific entry point */
int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevIns, LPSTR lpszArgument, int iShow)
{
    /* Convert argument list */
    int w_argc = 0;
    LPWSTR* w_argv = CommandLineToArgvW(GetCommandLineW(), &w_argc);
    if(w_argv) {
        std::vector<std::string> argv_buf;
        argv_buf.reserve(w_argc);

        for (int i = 0; i < w_argc; ++i) {
            int w_len = lstrlenW(w_argv[i]);
            int len = WideCharToMultiByte(CP_ACP, 0, w_argv[i], w_len, NULL, 0, NULL, NULL);
            std::string s;
            s.resize(len);
            WideCharToMultiByte(CP_ACP, 0, w_argv[i], w_len, &s[0], len, NULL, NULL);
            argv_buf.push_back(s);
        }

        std::vector<char*> argv;
        argv.reserve(argv_buf.size());
        for (std::vector<std::string>::iterator i = argv_buf.begin(); i != argv_buf.end(); ++i)
            argv.push_back((char*)i->c_str());

        int code = main(argv.size(), &argv[0]);

        LocalFree(w_argv);
        return code;
    }

    int code = main(0, NULL);

    LocalFree(w_argv);
    return code;
}

#endif
