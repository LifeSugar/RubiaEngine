#include "shader/ExternalShaderCompiler.hpp"
#include "SlangCompilerConfig.hpp"
#include "shader/SlangTarget.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>
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

namespace rubia::importer::shader::detail {
asset::ShaderAsset::CreateInfo
compileExternalShader(const std::filesystem::path &source,
                      asset::ShaderStage stage, const std::string &entryPoint,
                      const ShaderTarget& requestedTarget) {
  const auto target = resolveSlangTarget(requestedTarget);
  if (entryPoint.empty() ||
      (entryPoint.front() >= '0' && entryPoint.front() <= '9') ||
      entryPoint.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
          std::string::npos)
    throw std::invalid_argument("Shader entry point must be an identifier");
  const char *stageName = nullptr;
  switch (stage) {
  case asset::ShaderStage::Vertex:
    stageName = "vertex";
    break;
  case asset::ShaderStage::Fragment:
    stageName = "fragment";
    break;
  case asset::ShaderStage::Compute:
    stageName = "compute";
    break;
  default:
    throw std::invalid_argument("Unsupported shader stage");
  }
  const std::string label = "Slang";
  const auto absolute = std::filesystem::canonical(source);
#ifdef _WIN32
  const auto compiler =
      std::filesystem::u8path(slangCompilerExecutable).wstring();
  const auto processId = GetCurrentProcessId();
#else
  const std::string compiler = slangCompilerExecutable;
  const auto processId = getpid();
#endif
  static std::atomic<uint64_t> sequence{0};
  auto temporary =
      std::filesystem::temp_directory_path() /
      ("rubia-shader-" + std::to_string(processId) + "-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       "-" + std::to_string(++sequence));
  if (!std::filesystem::create_directory(temporary))
    throw std::runtime_error("Cannot create shader compilation directory");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } cleanup{temporary};
  const auto output = temporary / "shader.output";
  const auto log = temporary / "diagnostics.txt";
  auto sourceArgument = absolute.u8string();
#ifdef __APPLE__
  // Some compilers cannot open some absolute UTF-8 paths on macOS. Only the
  // child chdirs.
  sourceArgument = absolute.filename().u8string();
#endif
  std::vector<std::string> arguments;
  arguments = {sourceArgument,
               "-target",
               target.argument,
               "-profile",
               target.target.profile,
               "-entry",
               entryPoint,
               "-stage",
               stageName,
               "-matrix-layout-column-major",
               "-I",
               absolute.parent_path().u8string(),
               "-o",
               output.u8string()};

  if (target.target.backend == ShaderBackend::Vulkan)
  {
    arguments.push_back("-fvk-use-entrypoint-name");
    arguments.push_back("-emit-spirv-directly");
  }

#ifdef _WIN32
  struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
      if (value && value != INVALID_HANDLE_VALUE)
        CloseHandle(value);
    }
  };
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle diagnostics{CreateFileW(
      log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (diagnostics.value == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot open shader compiler diagnostics");
  Handle input{CreateFileW(L"NUL", GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  // Quote each argv element for the Windows CRT parser; never invoke a shell.
  const auto quote = [](const std::wstring &argument) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const auto c : argument) {
      if (c == L'\\') {
        ++backslashes;
        continue;
      }
      result.append(backslashes * (c == L'"' ? 2 : 1), L'\\');
      if (c == L'"')
        result += L'\\';
      result += c;
      backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    return result + L"\"";
  };
  std::wstring command = quote(compiler);
  for (const auto &argument : arguments)
    command += L" " + quote(std::filesystem::u8path(argument).wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = startup.hStdError = diagnostics.value;
  startup.hStdInput = input.value;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(compiler.c_str(), command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, absolute.parent_path().c_str(),
                      &startup, &process))
    throw std::runtime_error("Cannot launch " + label +
                             " compiler: " + std::to_string(GetLastError()));
  Handle processHandle{process.hProcess}, threadHandle{process.hThread};
  const auto wait = WaitForSingleObject(processHandle.value, 60000);
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(processHandle.value, 1);
    WaitForSingleObject(processHandle.value, INFINITE);
    throw std::runtime_error(label + " compilation timed out");
  }
  DWORD exitCode = 1;
  GetExitCodeProcess(processHandle.value, &exitCode);
#else
  struct FileActions {
    posix_spawn_file_actions_t value;
    FileActions() {
      const auto error = posix_spawn_file_actions_init(&value);
      if (error)
        throw std::runtime_error("Cannot initialize shader compiler process: " +
                                 std::string(std::strerror(error)));
    }
    ~FileActions() { posix_spawn_file_actions_destroy(&value); }
  } actions;
  auto checkAction = [](int error) {
    if (error)
      throw std::runtime_error("Cannot redirect shader compiler streams: " +
                               std::string(std::strerror(error)));
  };
  checkAction(posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO,
                                               "/dev/null", O_RDONLY, 0));
  checkAction(posix_spawn_file_actions_addopen(
      &actions.value, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
      0600));
  checkAction(posix_spawn_file_actions_adddup2(&actions.value, STDOUT_FILENO,
                                               STDERR_FILENO));
#ifdef __APPLE__
  // Source compilers on macOS fails to open absolute paths with non-ASCII
  // directory names. Change only the child's directory; imports run on
  // background threads.
  checkAction(posix_spawn_file_actions_addchdir_np(
      &actions.value, absolute.parent_path().c_str()));
#endif
  arguments.insert(arguments.begin(), compiler);
  std::vector<char *> argv;
  for (auto &argument : arguments)
    argv.push_back(argument.data());
  argv.push_back(nullptr);
  pid_t child = -1;
#ifdef __APPLE__
  auto environment = *_NSGetEnviron();
#else
  auto environment = environ;
#endif
  const auto error = posix_spawnp(&child, compiler.c_str(), &actions.value,
                                  nullptr, argv.data(), environment);
  if (error)
    throw std::runtime_error("Cannot launch " + label +
                             " compiler (check its installation): " +
                             std::string(std::strerror(error)));
  int status = 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  for (;;) {
    const auto result = waitpid(child, &status, WNOHANG);
    if (result == child)
      break;
    if (result < 0 && errno != EINTR)
      throw std::runtime_error("Cannot wait for " + label + " compiler: " +
                               std::string(std::strerror(errno)));
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(child, SIGKILL);
      while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
      }
      throw std::runtime_error(label + " compilation timed out");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
  if (exitCode != 0) {
    std::ifstream stream(log, std::ios::binary);
    std::string message((std::istreambuf_iterator<char>(stream)), {});
    throw std::runtime_error(label + " compilation failed:\n" + message);
  }
  asset::ShaderAsset::CreateInfo result;
  result.name = absolute.filename().u8string() + " (" + entryPoint + ")";
  result.stage = stage;
  result.entryPoint = entryPoint;
  std::ifstream binary(output, std::ios::binary | std::ios::ate);
  const auto size = binary.tellg();
  if (size <= 0)
    throw std::runtime_error("Empty Slang shader output");
  result.artifact.target = target.target;
  result.artifact.format = target.format;
  result.artifact.bytes.resize(static_cast<size_t>(size));
  binary.seekg(0);
  if (!binary.read(reinterpret_cast<char *>(result.artifact.bytes.data()), size))
    throw std::runtime_error("Cannot read Slang output");
  return result;
}
} // namespace rubia::importer::shader::detail
