#include "shader/HlslShaderCompiler.hpp"
#include "asset/AssetManager.hpp"
#include "shader/SpirvShaderImporter.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char **environ;
#endif
#endif
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
    if (entryPoint.empty() ||
        entryPoint.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("Shader entry point must be an identifier");
    const auto absolute = std::filesystem::canonical(source);
#ifdef _WIN32
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
    const auto processId = GetCurrentProcessId();
#else
    std::string compiler = "dxc";
    if (const auto *sdk = std::getenv("VULKAN_SDK"))
    {
        const auto candidate = std::filesystem::path(sdk) / "bin/dxc";
        if (access(candidate.c_str(), X_OK) == 0)
            compiler = candidate.string();
    }
    const auto processId = getpid();
#endif
    static std::atomic<uint64_t> sequence{0};
    auto temporary = std::filesystem::temp_directory_path() /
                     ("rubia-shader-" + std::to_string(processId) + "-" +
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
#ifdef _WIN32
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
#else
    struct FileActions
    {
        posix_spawn_file_actions_t value;
        FileActions()
        {
            const auto error = posix_spawn_file_actions_init(&value);
            if (error) throw std::runtime_error("Cannot initialize DXC process: " + std::string(std::strerror(error)));
        }
        ~FileActions() { posix_spawn_file_actions_destroy(&value); }
    } actions;
    auto checkAction = [](int error) {
        if (error) throw std::runtime_error("Cannot redirect DXC streams: " + std::string(std::strerror(error)));
    };
    checkAction(posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
    checkAction(posix_spawn_file_actions_addopen(&actions.value, STDOUT_FILENO, log.c_str(),
                                                O_WRONLY | O_CREAT | O_TRUNC, 0600));
    checkAction(posix_spawn_file_actions_adddup2(&actions.value, STDOUT_FILENO, STDERR_FILENO));
    auto sourceArgument = absolute.string();
#ifdef __APPLE__
    // DXC on macOS fails to open absolute paths with non-ASCII directory names.
    // Change only the child's directory; imports run on background threads.
    checkAction(posix_spawn_file_actions_addchdir_np(&actions.value, absolute.parent_path().c_str()));
    sourceArgument = absolute.filename().string();
#endif
    const std::string profile = stage == asset::ShaderStage::Vertex ? "vs_6_0"
                                : stage == asset::ShaderStage::Fragment ? "ps_6_0" : "cs_6_0";
    std::string arguments[] = {compiler, "-spirv", "-T", profile, "-E", entryPoint,
                               "-Fo", output.string(), sourceArgument};
    char *argv[10]{};
    for (size_t i = 0; i < 9; ++i) argv[i] = arguments[i].data();
    pid_t child = -1;
#ifdef __APPLE__
    auto environment = *_NSGetEnviron();
#else
    auto environment = environ;
#endif
    const auto error = posix_spawnp(&child, compiler.c_str(), &actions.value, nullptr, argv, environment);
    if (error)
        throw std::runtime_error("Cannot launch DXC (install Vulkan SDK or add dxc to PATH): " +
                                 std::string(std::strerror(error)));
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;)
    {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0 && errno != EINTR)
            throw std::runtime_error("Cannot wait for DXC: " + std::string(std::strerror(errno)));
        if (std::chrono::steady_clock::now() >= deadline)
        {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error("DXC compilation timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
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
}
} // namespace rubia::importer::shader
