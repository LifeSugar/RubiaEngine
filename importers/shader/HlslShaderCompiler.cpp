#include "shader/HlslShaderCompiler.hpp"
#include "asset/AssetManager.hpp"
#include "shader/SpirvShaderImporter.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef interface
#undef interface
#endif
#endif

namespace rubia::importer::shader
{
asset::ShaderAsset::CreateInfo HlslShaderCompiler::compile(const std::filesystem::path &source,
                                                           asset::ShaderStage stage,
                                                           const std::string &entryPoint) const
{
#ifdef _WIN32
    if (entryPoint.empty() ||
        entryPoint.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("Shader entry point must be an identifier");
    const auto absolute = std::filesystem::canonical(source);
    std::wstring compiler;
    wchar_t path[32768]{};
    const auto length = SearchPathW(nullptr, L"dxc.exe", nullptr, 32768, path, nullptr);
    if (length && length < 32768)
        compiler = path;
    if (compiler.empty())
    {
        const auto sdkLength = GetEnvironmentVariableW(L"VULKAN_SDK", path, 32768);
        if (sdkLength && sdkLength < 32768)
        {
            auto candidate = std::filesystem::path(path) / L"Bin/dxc.exe";
            if (std::filesystem::is_regular_file(candidate))
                compiler = candidate.wstring();
        }
    }
    if (compiler.empty())
        throw std::runtime_error("DXC not found. Install Vulkan SDK or add dxc.exe to PATH.");
    static std::atomic<uint64_t> sequence{0};
    auto temporary = std::filesystem::temp_directory_path() /
                     ("rubia-shader-" + std::to_string(GetCurrentProcessId()) + "-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                      "-" + std::to_string(++sequence));
    if (!std::filesystem::create_directory(temporary))
        throw std::runtime_error("Cannot create shader compilation directory");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{temporary};
    const auto output = temporary / "shader.spv";
    const auto log = temporary / "diagnostics.txt";
    struct Handle
    {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~Handle()
        {
            if (value && value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    };
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle diagnostics{CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (diagnostics.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open DXC diagnostics");
    Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    const wchar_t *profile = stage == asset::ShaderStage::Vertex     ? L"vs_6_0"
                             : stage == asset::ShaderStage::Fragment ? L"ps_6_0"
                                                                     : L"cs_6_0";
    // No shell. Windows paths cannot contain quotes; entry point is validated above.
    std::wstring command = L"\"" + compiler + L"\" -spirv -T " + profile + L" -E " +
                           std::wstring(entryPoint.begin(), entryPoint.end()) + L" -Fo \"" +
                           output.wstring() + L"\" \"" + absolute.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = diagnostics.value;
    startup.hStdInput = input.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(compiler.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, absolute.parent_path().c_str(), &startup, &process))
        throw std::runtime_error("Cannot launch DXC: " + std::to_string(GetLastError()));
    Handle processHandle{process.hProcess}, threadHandle{process.hThread};
    const auto wait = WaitForSingleObject(processHandle.value, 60000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(processHandle.value, 1);
        WaitForSingleObject(processHandle.value, INFINITE);
        throw std::runtime_error("DXC compilation timed out");
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(processHandle.value, &exitCode);
    if (exitCode != 0)
    {
        std::ifstream stream(log, std::ios::binary);
        std::string message((std::istreambuf_iterator<char>(stream)), {});
        throw std::runtime_error("HLSL compilation failed:\n" + message);
    }
    asset::AssetManager temporaryAssets;
    SpirvShaderImporter::CreateInfo info;
    info.assets = &temporaryAssets;
    info.path = output;
    info.stage = stage;
    info.entryPoint = entryPoint;
    const auto handle = SpirvShaderImporter{}.import(info);
    const auto &shader = temporaryAssets.shader(handle);
    return {absolute.filename().u8string() + " (" + entryPoint + ")", stage, entryPoint,
            shader.spirv(), shader.interface()};
#else
    static_cast<void>(source);
    static_cast<void>(stage);
    static_cast<void>(entryPoint);
    throw std::runtime_error("HLSL compiler process integration is currently Windows-only");
#endif
}
} // namespace rubia::importer::shader
