module;

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

export module Kairo.Editor.NativeDialogs;

export namespace kairo::editor
{
    namespace native_dialog_detail
    {
        [[nodiscard]] inline std::optional<std::filesystem::path> RunChooser(const char* command)
        {
#if defined(__APPLE__) || defined(__linux__)
            FILE* pipe = popen(command, "r");
            if (pipe == nullptr) throw std::runtime_error("Cannot start the native file chooser.");
            std::array<char, 4096> buffer{};
            std::string output;
            while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
                output.append(buffer.data());
            const int status = pclose(pipe);
            while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
            if (status != 0 || output.empty()) return std::nullopt;
            return std::filesystem::path(output);
#else
            (void)command;
            throw std::runtime_error("Native file dialogs are not available on this platform build.");
#endif
        }
    }

    /// Output: a user-selected Kairo project descriptor, or no value when the
    /// native dialog is cancelled. No shell input is interpolated into the
    /// command; prompts and filters are fixed application resources.
    [[nodiscard]] inline std::optional<std::filesystem::path> ChooseProjectFile()
    {
#if defined(__APPLE__)
        return native_dialog_detail::RunChooser(
            "osascript -e 'POSIX path of (choose file with prompt \"Open Kairo Project (.kproject)\")' 2>/dev/null");
#elif defined(__linux__)
        return native_dialog_detail::RunChooser(
            "zenity --file-selection --title='Open Kairo Project' --file-filter='Kairo projects | *.kproject' 2>/dev/null");
#else
        return native_dialog_detail::RunChooser("");
#endif
    }

    /// Output: an existing parent directory selected by the user. Callers keep
    /// ownership of destination naming and non-destructive existence checks.
    [[nodiscard]] inline std::optional<std::filesystem::path> ChooseProjectParentDirectory()
    {
#if defined(__APPLE__)
        return native_dialog_detail::RunChooser(
            "osascript -e 'POSIX path of (choose folder with prompt \"Choose a parent folder for the Kairo project\")' 2>/dev/null");
#elif defined(__linux__)
        return native_dialog_detail::RunChooser(
            "zenity --file-selection --directory --title='Choose a parent folder for the Kairo project' 2>/dev/null");
#else
        return native_dialog_detail::RunChooser("");
#endif
    }
}
