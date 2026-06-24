#include <exception>
#include <format>
#include <filesystem>
#include <vector>
#include <span>
#include <ranges>
#include <charconv>
#include <Windows.h>
#include <gdiplus.h>
#include <commdlg.h>

import console;
import cmdline;

namespace ansi = console::ansi;

template <class Func, class T> struct scope_exit {
    Func func;
    T& data;
    ~scope_exit() { func(data); }
};


auto load_texture(const wchar_t* file_path, const std::span<int> sizes) -> std::vector<std::pair<std::vector<uint8_t>, uint64_t>> {
    Gdiplus::GdiplusStartupInput gdiplus_startup_input;
    ULONG_PTR gdiplus_token;

    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, nullptr);
    scope_exit auto_shutdown_gdi(Gdiplus::GdiplusShutdown, gdiplus_token);

    auto origin_bmp = Gdiplus::Bitmap(file_path);
    if (origin_bmp.GetLastStatus() != Gdiplus::Ok)
        throw std::runtime_error("Failed to Load Image File");

    auto textures = std::vector<std::pair<std::vector<uint8_t>, uint64_t>>();

    for (int texture_size : sizes) {
        auto bmp = Gdiplus::Bitmap(texture_size, texture_size, PixelFormat32bppARGB);
        auto graphics = Gdiplus::Graphics(&bmp);

        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.Clear(Gdiplus::Color::Transparent);

        Gdiplus::GraphicsPath path;
        path.AddEllipse(0, 0, texture_size, texture_size);
        graphics.SetClip(&path);

        graphics.TranslateTransform(0, (Gdiplus::REAL)texture_size);
        graphics.ScaleTransform(1.0f, -1.0f);
        graphics.DrawImage(&origin_bmp, 0, 0, texture_size, texture_size);

        auto rect = Gdiplus::Rect(0, 0, texture_size, texture_size);
        Gdiplus::BitmapData bmp_data;

        if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bmp_data) != Gdiplus::Ok)
            throw std::runtime_error("Failed to Lock bits");
        
        std::vector<uint8_t> texture(texture_size * texture_size * 4);
        const uint8_t* __restrict src = (uint8_t*)bmp_data.Scan0;
        for (int i = 0; i < texture_size * texture_size * 4; i += 4) {
            texture[i + 0] = src[i + 2];
            texture[i + 1] = src[i + 1];
            texture[i + 2] = src[i + 0];
            texture[i + 3] = src[i + 3];
        }

        bmp.UnlockBits(&bmp_data);
        textures.emplace_back(std::move(texture), uint64_t(texture_size));
    }

    return textures;
}


template <class Func>
auto get_module_func(HMODULE module, const char* func_name) -> Func {
    if(auto func = (Func)(void*)GetProcAddress(module, func_name); !func)
        throw std::runtime_error(std::format("Cannot find function '{}' in module", func_name));
    else return func;
}


auto open_file_picker(const wchar_t* filter, const std::filesystem::path& initial_dir = L"") -> std::wstring {
    OPENFILENAMEW ofn = {};
    wchar_t file_path[MAX_PATH] = {0};
    
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initial_dir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == true)
        return std::wstring(ofn.lpstrFile);

    return L"";
}


void start_texdbg_pipe(const std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& textures) {
    constexpr char pipe_name[] = "\\\\.\\pipe\\D3D11_TexDbg_SharedBuffer";

    HANDLE pipe = CreateNamedPipeA(pipe_name, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 0, 0, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE)
        throw std::runtime_error(std::format("Failed to Create Named Pipe '{}{}{}'", ansi::blue, pipe_name, ansi::reset));

    scope_exit auto_close_pipe(CloseHandle, pipe);

    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
        throw std::runtime_error(std::format("Failed to Connect to Named Pipe '{}{}{}'", ansi::blue, pipe_name, ansi::reset));

    DWORD written = 0;
    int64_t textures_count = textures.size();
    WriteFile(pipe, &textures_count, sizeof(textures_count), &written, nullptr);

    for (auto& [texture, texture_size] : textures) {
        WriteFile(pipe, &texture_size, sizeof(texture_size), &written, nullptr);
        WriteFile(pipe, texture.data(), texture.size(), &written, nullptr);
    }
}


auto convert_to_int = std::views::transform([](std::ranges::viewable_range auto&& r) {
    int value = 0;
    auto [ptr, ec] = std::from_chars(r.data(), r.data() + r.size(), value);
    if (ec != std::errc())
        throw std::runtime_error(std::format("Cannot convert '{}' to int", std::string_view(r)));
    return value;
});


auto main(int argc, char* argv[]) -> int try {
    console::init();
    auto args = cmdline::parse(argc, argv);

    std::vector<int> sizes = args["--sizes"] | "512,256,128" | std::views::split(',') | convert_to_int | std::ranges::to<std::vector<int>>();
    std::string module_path = args["--module"] | args["-m"] | "dx11_texture_debugger.dll";
    std::string target_process_name = args["--target"] | args["-t"] | "";
    bool multi_texture = static_cast<bool>(args["--multi"]);

    console::info("DirectX 11 Texture Debugger {}v1.0.4{}", ansi::blue, ansi::reset);
    console::info("作者：{}SyrieYume & VanillaNahida{}", ansi::blue, ansi::reset);
    console::warn("免责声明：");
    console::warn("本项目仅供图形学学习、底层API拦截技术研究以及自研D3D11程序的调试使用。");
    console::warn("{}严禁{}用于任何带有反作弊系统或最终用户许可协议（EULA）禁止修改的商业游戏。", ansi::red, ansi::reset);
    console::warn("因{}不当使用{}导致的封号、法律纠纷与本项目无关，由使用者自行承担全部风险与责任。", ansi::orange, ansi::reset);
    console::warn("原作者：{}@SyrieYume{} 源仓库地址：{}https://github.com/SyrieYume/dx11_tex_dbg{}", ansi::blue, ansi::reset, ansi::blue, ansi::reset);
    console::warn("本程序以{}GPL-3.0 license{}协议开源在此处：{}https://github.com/VanillaNahida/dx11_tex_dbg_for_mw{}", ansi::blue, ansi::reset, ansi::blue, ansi::reset);
    console::info("Target Process Name: '{}{}{}'", ansi::blue, target_process_name, ansi::reset);

    HMODULE module = LoadLibraryA(module_path.c_str());
    if(!module)
        throw std::runtime_error(std::format("Failed to Load '{}{}{}'", ansi::blue, module_path, ansi::reset));

    scope_exit auto_free_module(FreeLibrary, module);
    console::info("Successfully Load '{}{}{}'", ansi::blue, module_path, ansi::reset);

    std::vector<std::pair<std::vector<uint8_t>, uint64_t>> textures;

    if (multi_texture) {
        console::info("启用多图模式");
        console::info("请选择用于 128x128 的图片，该图片将用在聊天和好友列表中作为小图使用：");
        std::wstring texture_file_128 = open_file_picker(L"图片文件(*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All Files\0*.*\0");
        
        console::info("请选择用于 512x512 的图片，该图片将用在点击查看头像后显示的大图片：");
        std::wstring texture_file_512 = open_file_picker(L"图片文件(*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All Files\0*.*\0");

        std::vector<int> size_128{128};
        std::vector<int> size_512{512};
        auto textures_128 = load_texture(texture_file_128.c_str(), size_128);
        auto textures_512 = load_texture(texture_file_512.c_str(), size_512);

        textures.insert(textures.end(), textures_512.begin(), textures_512.end());
        textures.insert(textures.end(), textures_128.begin(), textures_128.end());
    } else {
        console::info("未启用双头像支持，将使用默认的单头像模式。请在打开的窗口中选择头像文件。");
        std::wstring texture_file = open_file_picker(L"图片文件(*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All Files\0*.*\0");
        textures = load_texture(texture_file.c_str(), sizes);
    }
    console::info("已成功加载图片，请在游戏内点击自定义头像旁边的相机按钮然后继续...", sizes[0], sizes[1]);
    console::info("Successfully Load Debug Texture Image");

    auto func_hook_proc = get_module_func<HOOKPROC>(module, "hook_proc");
    auto func_set_target = get_module_func<void(*)(const char*)>(module, "set_target");

    func_set_target(target_process_name.c_str());
    
    HHOOK hook = SetWindowsHookExW(WH_CBT, func_hook_proc, module, 0);
    if(!hook)
        throw std::runtime_error("Unable to Set Windows Hook");

    scope_exit auto_unhook(UnhookWindowsHookEx, hook);
    console::info("Successfully Set Windows Hook");
    
    console::info("Waiting for the Texture data to be taken by Target...");
    start_texdbg_pipe(textures);
    console::info("Texture data was Successfully taken by Target");

    console::info("请按{}Enter{}键退出...", ansi::blue, ansi::reset);
    // 等待用户确认是否继续
    std::getchar();
    return 0;
}

catch(const std::exception& e) {
    console::println("{}[Error]{} {}", ansi::red, ansi::reset, e.what());
    system("pause");
}