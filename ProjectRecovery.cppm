module;

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.ProjectRecovery;

import Kairo.Assets;
import Kairo.Editor.DocumentSerialization;
import Kairo.Editor.ProjectDescriptor;
import Kairo.Editor.ProjectDocuments;
import Kairo.Editor.ProjectPaths;
import Kairo.Editor.TextFormat;
import Kairo.EngineCore;

export namespace kairo::editor
{
    inline constexpr std::size_t DefaultRecoverySnapshotLimit = 10u;
    inline constexpr std::size_t MaximumRecoverySnapshotLimit = 64u;

    enum class RecoveryFileRole : std::uint8_t
    {
        ProjectDescriptor,
        AssetManifest,
        Scene,
        AuthoringDocument,
        TextDraft
    };

    struct RecoveryFile final
    {
        RecoveryFileRole Role = RecoveryFileRole::Scene;
        std::filesystem::path TargetPath;
        std::filesystem::path PayloadPath;
        std::uint64_t ByteCount = 0u;
        std::uint64_t Checksum = 0u;
        bool WasDirty = false;
        bool WasActive = false;
    };

    struct RecoverySnapshot final
    {
        std::filesystem::path Directory;
        std::filesystem::path OriginalProjectRoot;
        std::filesystem::path ProjectFile;
        std::filesystem::path ActiveScene;
        std::int64_t CreatedUnixMilliseconds = 0;
        std::vector<RecoveryFile> Files;
    };

    /// One editor text buffer associated with an open canonical document.
    /// Draft source may be syntactically invalid; recovery must preserve user
    /// bytes without promoting them to authoritative project state.
    struct RecoveryDocumentDraft final
    {
        kairo::assets::AssetID DocumentID;
        std::filesystem::path DocumentPath;
        std::string Source;
        bool Active = false;
    };

    class RecoveryFormatError final : public std::runtime_error
    {
    public:
        RecoveryFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo recovery manifest " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}

        std::size_t Line;
        std::size_t Column;
    };

    namespace recovery_detail
    {
        constexpr std::size_t MaximumManifestBytes = 4u * 1024u * 1024u;
        constexpr std::size_t MaximumSnapshotFiles = 512u;
        constexpr std::uint64_t MaximumPayloadBytes = 512ull * 1024ull * 1024ull;

        [[nodiscard]] inline std::string_view Name(RecoveryFileRole role) noexcept
        {
            switch (role)
            {
                case RecoveryFileRole::ProjectDescriptor: return "project";
                case RecoveryFileRole::AssetManifest: return "assets";
                case RecoveryFileRole::Scene: return "scene";
                case RecoveryFileRole::AuthoringDocument: return "document";
                case RecoveryFileRole::TextDraft: return "text-draft";
            }
            return "unknown";
        }

        [[nodiscard]] inline std::optional<RecoveryFileRole> ParseRole(std::string_view value) noexcept
        {
            if (value == "project") return RecoveryFileRole::ProjectDescriptor;
            if (value == "assets") return RecoveryFileRole::AssetManifest;
            if (value == "scene") return RecoveryFileRole::Scene;
            if (value == "document") return RecoveryFileRole::AuthoringDocument;
            if (value == "text-draft") return RecoveryFileRole::TextDraft;
            return std::nullopt;
        }

        [[nodiscard]] inline std::uint64_t Checksum(std::string_view source) noexcept
        {
            // FNV-1a is used as a corruption detector, not as a security primitive.
            std::uint64_t hash = 14695981039346656037ull;
            for (const unsigned char byte : source)
            {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template<class Integer>
        [[nodiscard]] inline Integer ParseInteger(const FormatToken& token,
            std::size_t line, std::string_view role)
        {
            Integer value{};
            const auto [end, error] = std::from_chars(
                token.Text.data(), token.Text.data() + token.Text.size(), value);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw RecoveryFormatError(line, token.Column, std::string(role) + " is not a valid integer");
            return value;
        }

        [[nodiscard]] inline bool ParseBoolean(const FormatToken& token, std::size_t line)
        {
            if (token.Text == "true") return true;
            if (token.Text == "false") return false;
            throw RecoveryFormatError(line, token.Column, "expected true or false");
        }

        [[nodiscard]] inline std::filesystem::path SafeRelativePath(
            const FormatToken& token, std::size_t line, std::string_view role)
        {
            try { return kairo::assets::NormalizeAssetPath(token.Text); }
            catch (const std::exception& error)
            {
                throw RecoveryFormatError(line, token.Column,
                    std::string(role) + " is unsafe: " + error.what());
            }
        }

        [[nodiscard]] inline std::filesystem::path RecoveryRoot(
            const std::filesystem::path& projectRoot)
        {
            return projectRoot / ".kairo" / "recovery";
        }

        [[nodiscard]] inline std::filesystem::path CanonicalSnapshotDirectory(
            const std::filesystem::path& path)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(path, error))
            {
                if (error) throw std::runtime_error("Cannot inspect recovery snapshot: " + error.message());
                throw std::invalid_argument("Recovery snapshot directory does not exist: " + path.string());
            }
            auto canonical = std::filesystem::canonical(path, error);
            if (error) throw std::runtime_error("Cannot canonicalize recovery snapshot: " + error.message());
            return canonical;
        }

        [[nodiscard]] inline std::string SnapshotName(std::int64_t milliseconds)
        {
            return "snapshot-" + std::to_string(milliseconds) + "-" +
                kairo::assets::GenerateAssetID().ToString();
        }

        inline void WritePayload(const std::filesystem::path& staging,
            RecoverySnapshot& snapshot, RecoveryFileRole role,
            const std::filesystem::path& target, std::string source,
            bool dirty = false, bool active = false,
            const std::filesystem::path& explicitPayload = {})
        {
            if (snapshot.Files.size() >= MaximumSnapshotFiles)
                throw std::length_error("Recovery snapshot exceeds its 512-file safety limit.");
            const auto safeTarget = kairo::assets::NormalizeAssetPath(target);
            const auto payload = explicitPayload.empty()
                ? std::filesystem::path("payload") / safeTarget
                : kairo::assets::NormalizeAssetPath(explicitPayload);
            if (source.size() > MaximumPayloadBytes)
                throw std::length_error("One recovery payload exceeds its 512 MiB safety limit.");
            SaveTextFileAtomically(staging / payload, source, "recovery payload");
            snapshot.Files.push_back({ role, safeTarget, payload,
                static_cast<std::uint64_t>(source.size()), Checksum(source), dirty, active });
        }

        [[nodiscard]] inline std::string SerializeManifest(const RecoverySnapshot& snapshot)
        {
            std::ostringstream output;
            output << "kairo-recovery 1\n";
            output << "created-unix-ms " << snapshot.CreatedUnixMilliseconds << '\n';
            output << "project-root " << QuoteFormatText(snapshot.OriginalProjectRoot.generic_string()) << '\n';
            output << "project-file " << QuoteFormatText(snapshot.ProjectFile.generic_string()) << '\n';
            output << "active-scene " << QuoteFormatText(snapshot.ActiveScene.generic_string()) << '\n';
            for (const RecoveryFile& file : snapshot.Files)
                output << "file " << Name(file.Role) << ' '
                    << QuoteFormatText(file.TargetPath.generic_string()) << ' '
                    << QuoteFormatText(file.PayloadPath.generic_string()) << ' '
                    << file.ByteCount << ' ' << file.Checksum << ' '
                    << (file.WasDirty ? "true" : "false") << ' '
                    << (file.WasActive ? "true" : "false") << '\n';
            return output.str();
        }

        inline void ValidatePayloads(const RecoverySnapshot& snapshot)
        {
            for (const RecoveryFile& file : snapshot.Files)
            {
                const auto source = LoadBoundedTextFile(snapshot.Directory / file.PayloadPath,
                    MaximumPayloadBytes, "recovery payload");
                if (source.size() != file.ByteCount || Checksum(source) != file.Checksum)
                    throw std::runtime_error("Recovery payload failed its size/checksum validation: " +
                        file.TargetPath.generic_string());
            }
        }
    }

    /// Input: a published snapshot directory containing `manifest.krecover`.
    /// Output: validated metadata; every payload is size/checksum verified.
    /// Task: reject malformed, path-traversing, oversized, incomplete, or
    /// corrupted recovery data before any authored project file can be touched.
    [[nodiscard]] inline RecoverySnapshot LoadRecoverySnapshot(
        const std::filesystem::path& snapshotDirectory)
    {
        using namespace recovery_detail;
        RecoverySnapshot result;
        result.Directory = CanonicalSnapshotDirectory(snapshotDirectory);
        const auto source = LoadBoundedTextFile(result.Directory / "manifest.krecover",
            MaximumManifestBytes, "Kairo recovery manifest");
        bool header = false;
        bool created = false;
        bool root = false;
        bool project = false;
        bool activeScene = false;
        std::istringstream input(source);
        std::string lineText;
        std::size_t line = 0u;
        while (std::getline(input, lineText))
        {
            ++line;
            const auto tokens = TokenizeFormatLine<RecoveryFormatError>(lineText, line);
            if (tokens.empty()) continue;
            if (!header)
            {
                RequireTokenCount<RecoveryFormatError>(tokens, 2u, line, "kairo-recovery header");
                if (tokens[0].Text != "kairo-recovery" || tokens[1].Text != "1")
                    throw RecoveryFormatError(line, tokens[0].Column, "expected supported kairo-recovery 1 header");
                header = true;
                continue;
            }
            if (tokens[0].Text == "created-unix-ms")
            {
                RequireTokenCount<RecoveryFormatError>(tokens, 2u, line, "created-unix-ms");
                if (created) throw RecoveryFormatError(line, 1u, "duplicate creation timestamp");
                result.CreatedUnixMilliseconds = ParseInteger<std::int64_t>(tokens[1], line, "creation timestamp");
                if (result.CreatedUnixMilliseconds < 0)
                    throw RecoveryFormatError(line, tokens[1].Column, "creation timestamp cannot be negative");
                created = true;
            }
            else if (tokens[0].Text == "project-root")
            {
                RequireTokenCount<RecoveryFormatError>(tokens, 2u, line, "project-root");
                if (root) throw RecoveryFormatError(line, 1u, "duplicate project root");
                result.OriginalProjectRoot = std::filesystem::path(tokens[1].Text).lexically_normal();
                if (!result.OriginalProjectRoot.is_absolute())
                    throw RecoveryFormatError(line, tokens[1].Column, "project root must be absolute");
                root = true;
            }
            else if (tokens[0].Text == "project-file" || tokens[0].Text == "active-scene")
            {
                RequireTokenCount<RecoveryFormatError>(tokens, 2u, line, tokens[0].Text);
                auto& seen = tokens[0].Text == "project-file" ? project : activeScene;
                if (seen) throw RecoveryFormatError(line, 1u, "duplicate session path");
                auto path = SafeRelativePath(tokens[1], line, tokens[0].Text);
                if (tokens[0].Text == "project-file") result.ProjectFile = std::move(path);
                else result.ActiveScene = std::move(path);
                seen = true;
            }
            else if (tokens[0].Text == "file")
            {
                RequireTokenCount<RecoveryFormatError>(tokens, 8u, line, "file");
                if (result.Files.size() >= MaximumSnapshotFiles)
                    throw RecoveryFormatError(line, 1u, "snapshot exceeds its 512-file safety limit");
                const auto role = ParseRole(tokens[1].Text);
                if (!role.has_value()) throw RecoveryFormatError(line, tokens[1].Column, "unknown recovery file role");
                RecoveryFile file{ *role,
                    SafeRelativePath(tokens[2], line, "target path"),
                    SafeRelativePath(tokens[3], line, "payload path"),
                    ParseInteger<std::uint64_t>(tokens[4], line, "payload byte count"),
                    ParseInteger<std::uint64_t>(tokens[5], line, "payload checksum"),
                    ParseBoolean(tokens[6], line), ParseBoolean(tokens[7], line) };
                if (file.ByteCount > MaximumPayloadBytes)
                    throw RecoveryFormatError(line, tokens[4].Column, "payload exceeds its safety limit");
                if (!file.PayloadPath.generic_string().starts_with("payload/"))
                    throw RecoveryFormatError(line, tokens[3].Column, "payload must remain inside payload/");
                const auto sameTarget = std::ranges::find_if(result.Files, [&](const RecoveryFile& existing) {
                    return kairo::assets::PortableAssetPathKey(existing.TargetPath) ==
                        kairo::assets::PortableAssetPathKey(file.TargetPath); });
                if (sameTarget != result.Files.end() &&
                    !((sameTarget->Role == RecoveryFileRole::AuthoringDocument &&
                        file.Role == RecoveryFileRole::TextDraft) ||
                      (sameTarget->Role == RecoveryFileRole::TextDraft &&
                        file.Role == RecoveryFileRole::AuthoringDocument)))
                    throw RecoveryFormatError(line, tokens[2].Column,
                        "duplicate recovery target path");
                if (std::ranges::any_of(result.Files, [&](const RecoveryFile& existing) {
                    return kairo::assets::PortableAssetPathKey(existing.PayloadPath) ==
                        kairo::assets::PortableAssetPathKey(file.PayloadPath); }))
                    throw RecoveryFormatError(line, tokens[3].Column,
                        "duplicate recovery payload path");
                result.Files.push_back(std::move(file));
            }
            else throw RecoveryFormatError(line, tokens[0].Column, "unknown statement");
        }
        if (!header || !created || !root || !project || !activeScene)
            throw RecoveryFormatError(line + 1u, 1u, "manifest is incomplete");
        const auto canonicalRoot = CanonicalProjectRoot(result.OriginalProjectRoot);
        std::error_code recoveryError;
        const auto expectedRecoveryRoot = std::filesystem::canonical(
            RecoveryRoot(canonicalRoot), recoveryError);
        if (recoveryError || result.Directory.parent_path() != expectedRecoveryRoot)
            throw RecoveryFormatError(line + 1u, 1u,
                "snapshot is not owned by the declared project's recovery directory");
        if (result.Files.empty()) throw RecoveryFormatError(line + 1u, 1u, "manifest contains no files");
        const auto countRole = [&](RecoveryFileRole role) {
            return std::ranges::count(result.Files, role, &RecoveryFile::Role);
        };
        if (countRole(RecoveryFileRole::ProjectDescriptor) != 1 ||
            countRole(RecoveryFileRole::AssetManifest) != 1 ||
            countRole(RecoveryFileRole::Scene) != 1)
            throw RecoveryFormatError(line + 1u, 1u, "manifest requires exactly one project, asset, and scene payload");
        for (const RecoveryFile& file : result.Files)
        {
            if (file.Role != RecoveryFileRole::TextDraft) continue;
            if (!std::ranges::any_of(result.Files, [&](const RecoveryFile& candidate) {
                return candidate.Role == RecoveryFileRole::AuthoringDocument &&
                    kairo::assets::PortableAssetPathKey(candidate.TargetPath) ==
                    kairo::assets::PortableAssetPathKey(file.TargetPath); }))
                throw RecoveryFormatError(line + 1u, 1u,
                    "text draft has no matching canonical document payload");
        }
        ValidatePayloads(result);
        return result;
    }

    /// Creates an isolated point-in-time project journal without calling any
    /// mutating save API. Dirty flags, authored files, and command history are
    /// therefore unchanged. A complete staging directory is renamed into view
    /// only after every payload and the checksummed manifest are durable.
    [[nodiscard]] inline RecoverySnapshot CreateRecoverySnapshot(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& projectFile,
        const ProjectDescriptor& descriptor,
        const std::filesystem::path& activeScene,
        const kairo::assets::AssetRegistry& assets,
        const kairo::engine::Scene& scene,
        const ProjectDocuments& documents,
        const std::vector<RecoveryDocumentDraft>& drafts,
        bool sceneDirty,
        bool assetsDirty,
        std::size_t retentionLimit = DefaultRecoverySnapshotLimit)
    {
        using namespace recovery_detail;
        if (retentionLimit == 0u || retentionLimit > MaximumRecoverySnapshotLimit)
            throw std::invalid_argument("Recovery retention must be between 1 and 64 snapshots.");
        const auto canonicalRoot = CanonicalProjectRoot(projectRoot);
        const auto relativeProject = std::filesystem::relative(projectFile, canonicalRoot);
        const auto safeProject = kairo::assets::NormalizeAssetPath(relativeProject);
        const auto safeScene = kairo::assets::NormalizeAssetPath(activeScene);
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto root = RecoveryRoot(canonicalRoot);
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error) throw std::runtime_error("Cannot create project recovery directory: " + error.message());
        const std::string name = SnapshotName(now);
        const auto staging = root / (name + ".staging");
        const auto published = root / name;
        RecoverySnapshot snapshot{ published, canonicalRoot, safeProject, safeScene, now, {} };
        try
        {
            WritePayload(staging, snapshot, RecoveryFileRole::ProjectDescriptor, safeProject,
                SerializeProjectDescriptor(descriptor));
            WritePayload(staging, snapshot, RecoveryFileRole::AssetManifest, descriptor.AssetManifest,
                kairo::assets::SerializeAssetManifest(assets), assetsDirty);
            WritePayload(staging, snapshot, RecoveryFileRole::Scene, safeScene,
                kairo::engine::SerializeScene(scene, assets), sceneDirty);
            for (const ProjectDocumentInfo& info : documents.Snapshot())
                WritePayload(staging, snapshot, RecoveryFileRole::AuthoringDocument,
                    info.RelativePath, SerializeDocument(documents.Get(info.ID)),
                    info.Dirty, info.Active);
            for (const RecoveryDocumentDraft& draft : drafts)
            {
                if (!documents.Contains(draft.DocumentID))
                    throw std::invalid_argument("Recovery text draft does not identify an open document.");
                if (kairo::assets::PortableAssetPathKey(documents.RelativePath(draft.DocumentID)) !=
                    kairo::assets::PortableAssetPathKey(draft.DocumentPath))
                    throw std::invalid_argument("Recovery text draft path disagrees with its open document.");
                if (std::ranges::count(drafts, draft.DocumentID,
                    &RecoveryDocumentDraft::DocumentID) != 1)
                    throw std::invalid_argument("Recovery contains duplicate text drafts for one document.");
                WritePayload(staging, snapshot, RecoveryFileRole::TextDraft,
                    draft.DocumentPath, draft.Source, true, draft.Active,
                    std::filesystem::path("payload/.drafts") /
                        (draft.DocumentID.ToString() + ".draft"));
            }
            SaveTextFileAtomically(staging / "manifest.krecover", SerializeManifest(snapshot),
                "Kairo recovery manifest");
            std::filesystem::rename(staging, published, error);
            if (error) throw std::runtime_error("Cannot publish recovery snapshot: " + error.message());
        }
        catch (...)
        {
            std::filesystem::remove_all(staging, error);
            throw;
        }

        // Only valid, published snapshot-* directories participate in rotation.
        std::vector<RecoverySnapshot> candidates;
        for (const auto& entry : std::filesystem::directory_iterator(root))
        {
            if (!entry.is_directory() || !entry.path().filename().string().starts_with("snapshot-")) continue;
            try { candidates.push_back(LoadRecoverySnapshot(entry.path())); }
            catch (...) { /* Preserve unknown/corrupt data for manual inspection. */ }
        }
        std::ranges::sort(candidates, [](const RecoverySnapshot& left, const RecoverySnapshot& right) {
            if (left.CreatedUnixMilliseconds != right.CreatedUnixMilliseconds)
                return left.CreatedUnixMilliseconds < right.CreatedUnixMilliseconds;
            return left.Directory.generic_string() < right.Directory.generic_string();
        });
        while (candidates.size() > retentionLimit)
        {
            const auto oldest = std::ranges::find_if(candidates,
                [&](const RecoverySnapshot& candidate) { return candidate.Directory != published; });
            if (oldest == candidates.end()) break;
            std::filesystem::remove_all(oldest->Directory, error);
            if (error) throw std::runtime_error("Cannot rotate old recovery snapshot: " + error.message());
            candidates.erase(oldest);
        }
        return LoadRecoverySnapshot(published);
    }

    /// Explicitly restores one validated journal into its original project.
    /// Existing targets are copied to a recovery-owned backup first. If any
    /// replacement fails, every touched path is rolled back before rethrowing.
    [[nodiscard]] inline std::filesystem::path RestoreRecoverySnapshot(
        const std::filesystem::path& snapshotDirectory)
    {
        using namespace recovery_detail;
        const RecoverySnapshot snapshot = LoadRecoverySnapshot(snapshotDirectory);
        const auto root = CanonicalProjectRoot(snapshot.OriginalProjectRoot);
        const auto backup = root / ".kairo" / "recovery-backups" /
            ("backup-" + std::to_string(snapshot.CreatedUnixMilliseconds) + "-" +
                kairo::assets::GenerateAssetID().ToString());
        std::vector<const RecoveryFile*> restorable;
        for (const RecoveryFile& file : snapshot.Files)
            if (file.Role != RecoveryFileRole::TextDraft) restorable.push_back(&file);
        std::vector<std::pair<std::filesystem::path, bool>> targets;
        std::error_code error;
        try
        {
            for (const RecoveryFile* file : restorable)
            {
                const auto target = ResolveProjectOutput(root, file->TargetPath);
                const bool existed = std::filesystem::is_regular_file(target, error);
                if (error) throw std::runtime_error("Cannot inspect recovery target: " + error.message());
                targets.emplace_back(target, existed);
                if (existed)
                {
                    const auto backupFile = backup / file->TargetPath;
                    std::filesystem::create_directories(backupFile.parent_path());
                    if (!std::filesystem::copy_file(target, backupFile,
                        std::filesystem::copy_options::none, error))
                        throw std::runtime_error("Cannot back up recovery target: " + error.message());
                }
            }
            for (std::size_t index = 0u; index < restorable.size(); ++index)
            {
                const auto source = LoadBoundedTextFile(snapshot.Directory / restorable[index]->PayloadPath,
                    MaximumPayloadBytes, "recovery payload");
                SaveTextFileAtomically(targets[index].first, source, "restored project file");
            }
        }
        catch (...)
        {
            for (std::size_t index = 0u; index < targets.size(); ++index)
            {
                if (targets[index].second)
                {
                    const auto backupFile = backup / restorable[index]->TargetPath;
                    if (std::filesystem::is_regular_file(backupFile, error))
                        std::filesystem::copy_file(backupFile, targets[index].first,
                            std::filesystem::copy_options::overwrite_existing, error);
                }
                else std::filesystem::remove(targets[index].first, error);
            }
            throw;
        }
        return root / snapshot.ProjectFile;
    }

    /// Reads one already-checksummed payload from a loaded snapshot. This is
    /// primarily used to restore raw text drafts into editor buffers.
    [[nodiscard]] inline std::string LoadRecoveryPayload(
        const RecoverySnapshot& snapshot, const RecoveryFile& file)
    {
        if (std::ranges::find_if(snapshot.Files, [&](const RecoveryFile& candidate) {
            return candidate.PayloadPath == file.PayloadPath; }) == snapshot.Files.end())
            throw std::invalid_argument("Recovery file does not belong to this snapshot.");
        const auto source = LoadBoundedTextFile(snapshot.Directory / file.PayloadPath,
            recovery_detail::MaximumPayloadBytes, "recovery payload");
        if (source.size() != file.ByteCount || recovery_detail::Checksum(source) != file.Checksum)
            throw std::runtime_error("Recovery payload failed size/checksum validation.");
        return source;
    }
}
