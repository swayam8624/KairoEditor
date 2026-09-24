module;

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

export module Kairo.Editor.RuntimePreviewProcess;

export namespace kairo::editor
{
    /// Owns one external runtime preview process started by the Editor Play action.
    /// The child receives the active .kproject path as its first argument.
    class RuntimePreviewProcess final
    {
    public:
        RuntimePreviewProcess() = default;
        RuntimePreviewProcess(const RuntimePreviewProcess&) = delete;
        RuntimePreviewProcess& operator=(const RuntimePreviewProcess&) = delete;

        ~RuntimePreviewProcess() noexcept { Stop(); }

        void Start(const std::filesystem::path& executable,
            const std::filesystem::path& project,
            const std::filesystem::path& workingDirectory)
        {
            if (Running())
                throw std::logic_error("A Kairo runtime preview is already running.");

            std::error_code error;
            const auto resolvedExecutable =
                std::filesystem::weakly_canonical(executable, error);
            if (error || !std::filesystem::is_regular_file(resolvedExecutable, error) || error)
                throw std::invalid_argument(
                    "Runtime preview executable is not a readable regular file: " +
                    executable.string());

            const auto resolvedProject = std::filesystem::weakly_canonical(project, error);
            if (error || !std::filesystem::is_regular_file(resolvedProject, error) || error)
                throw std::invalid_argument(
                    "Runtime preview project is not a readable regular file: " +
                    project.string());

            const auto resolvedWorking =
                std::filesystem::weakly_canonical(workingDirectory, error);
            if (error || !std::filesystem::is_directory(resolvedWorking, error) || error)
                throw std::invalid_argument(
                    "Runtime preview working directory is invalid: " +
                    workingDirectory.string());

#if defined(_WIN32)
            const std::wstring command =
                QuoteWindows(resolvedExecutable.wstring()) + L" " +
                QuoteWindows(resolvedProject.wstring());
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            const std::wstring working = resolvedWorking.wstring();
            if (!CreateProcessW(
                    resolvedExecutable.wstring().c_str(),
                    mutableCommand.data(),
                    nullptr, nullptr, FALSE, 0, nullptr,
                    working.c_str(), &startup, &process))
                throw std::runtime_error(
                    "CreateProcessW failed while launching the Kairo runtime preview.");

            CloseHandle(process.hThread);
            m_Process = process.hProcess;
#else
            const pid_t child = fork();
            if (child < 0)
                throw std::runtime_error(
                    "fork failed while launching the Kairo runtime preview.");
            if (child == 0)
            {
                if (chdir(resolvedWorking.c_str()) != 0)
                    _exit(126);
                const std::string exe = resolvedExecutable.string();
                const std::string projectPath = resolvedProject.string();
                std::vector<char*> arguments;
                arguments.push_back(const_cast<char*>(exe.c_str()));
                arguments.push_back(const_cast<char*>(projectPath.c_str()));
                arguments.push_back(nullptr);
                execv(exe.c_str(), arguments.data());
                _exit(127);
            }
            m_Process = child;
#endif
        }

        [[nodiscard]] bool Running() noexcept
        {
#if defined(_WIN32)
            if (m_Process == nullptr) return false;
            const DWORD result = WaitForSingleObject(m_Process, 0);
            if (result == WAIT_TIMEOUT) return true;
            CloseHandle(m_Process);
            m_Process = nullptr;
            return false;
#else
            if (m_Process <= 0) return false;
            int status = 0;
            const pid_t result = waitpid(m_Process, &status, WNOHANG);
            if (result == 0) return true;
            if (result == m_Process)
            {
                m_Process = -1;
                return false;
            }
            if (result < 0) m_Process = -1;
            return false;
#endif
        }

        void Stop() noexcept
        {
#if defined(_WIN32)
            if (m_Process == nullptr) return;
            if (WaitForSingleObject(m_Process, 0) == WAIT_TIMEOUT)
            {
                (void)TerminateProcess(m_Process, 0);
                (void)WaitForSingleObject(m_Process, 2000);
            }
            CloseHandle(m_Process);
            m_Process = nullptr;
#else
            if (m_Process <= 0) return;
            int status = 0;
            if (waitpid(m_Process, &status, WNOHANG) == 0)
            {
                (void)kill(m_Process, SIGTERM);
                for (int attempt = 0; attempt < 20; ++attempt)
                {
                    if (waitpid(m_Process, &status, WNOHANG) == m_Process)
                    {
                        m_Process = -1;
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                (void)kill(m_Process, SIGKILL);
                (void)waitpid(m_Process, &status, 0);
            }
            m_Process = -1;
#endif
        }

    private:
#if defined(_WIN32)
        HANDLE m_Process = nullptr;

        [[nodiscard]] static std::wstring QuoteWindows(std::wstring_view value)
        {
            std::wstring result = L"\"";
            std::size_t backslashes = 0u;
            for (const wchar_t character : value)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (character == L'\"')
                {
                    result.append(backslashes * 2u + 1u, L'\\');
                    result.push_back(L'\"');
                    backslashes = 0u;
                    continue;
                }
                result.append(backslashes, L'\\');
                backslashes = 0u;
                result.push_back(character);
            }
            result.append(backslashes * 2u, L'\\');
            result.push_back(L'\"');
            return result;
        }
#else
        pid_t m_Process = -1;
#endif
    };
}
