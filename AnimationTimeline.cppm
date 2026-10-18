module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.AnimationTimeline;

import Kairo.Assets;

export namespace kairo::editor
{
    /// Explicit policy for edits that target an already occupied key time.
    /// Authoring tools must never silently destroy a key as a side effect of a
    /// drag operation, so callers choose between rejection and replacement.
    enum class AnimationKeyCollisionPolicy : std::uint8_t
    {
        Reject = 1u,
        Replace = 2u
    };

    struct AnimationTimelineKey final
    {
        float TimeSeconds = 0.0f;
        std::array<float, 4u> Value{};
        std::array<float, 4u> InTangent{};
        std::array<float, 4u> OutTangent{};

        friend bool operator==(const AnimationTimelineKey&,
            const AnimationTimelineKey&) = default;
    };

    struct AnimationTrackIdentity final
    {
        std::uint32_t TargetNode = kairo::assets::GltfMissingIndex;
        kairo::assets::GltfAnimationPath Path =
            kairo::assets::GltfAnimationPath::Translation;

        friend bool operator==(const AnimationTrackIdentity&,
            const AnimationTrackIdentity&) = default;
    };

    struct AnimationTimelineTrack final
    {
        AnimationTrackIdentity Identity{};
        kairo::assets::GltfAnimationInterpolation Interpolation =
            kairo::assets::GltfAnimationInterpolation::Linear;
        std::vector<AnimationTimelineKey> Keys;

        friend bool operator==(const AnimationTimelineTrack&,
            const AnimationTimelineTrack&) = default;
    };

    namespace animation_timeline_detail
    {
        [[nodiscard]] inline bool Finite(const std::array<float, 4u>& value) noexcept
        {
            return std::ranges::all_of(value,
                [](float component) { return std::isfinite(component); });
        }

        inline void ValidatePath(kairo::assets::GltfAnimationPath path)
        {
            switch (path)
            {
                case kairo::assets::GltfAnimationPath::Translation:
                case kairo::assets::GltfAnimationPath::Rotation:
                case kairo::assets::GltfAnimationPath::Scale:
                    return;
            }
            throw std::invalid_argument("Animation timeline track path is invalid.");
        }

        inline void ValidateInterpolation(
            kairo::assets::GltfAnimationInterpolation interpolation)
        {
            switch (interpolation)
            {
                case kairo::assets::GltfAnimationInterpolation::Linear:
                case kairo::assets::GltfAnimationInterpolation::Step:
                case kairo::assets::GltfAnimationInterpolation::CubicSpline:
                    return;
            }
            throw std::invalid_argument(
                "Animation timeline interpolation mode is invalid.");
        }

        [[nodiscard]] inline AnimationTimelineKey CanonicalizeKey(
            kairo::assets::GltfAnimationPath path,
            AnimationTimelineKey key)
        {
            ValidatePath(path);
            if (!std::isfinite(key.TimeSeconds) || key.TimeSeconds < 0.0f)
                throw std::invalid_argument(
                    "Animation key time must be finite and non-negative.");
            if (!Finite(key.Value) || !Finite(key.InTangent) ||
                !Finite(key.OutTangent))
                throw std::invalid_argument(
                    "Animation key values and tangents must be finite.");

            if (path == kairo::assets::GltfAnimationPath::Rotation)
            {
                double lengthSquared = 0.0;
                for (const float component : key.Value)
                    lengthSquared += static_cast<double>(component) * component;
                if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12)
                    throw std::invalid_argument(
                        "Animation rotation key requires a non-zero quaternion.");
                const float inverseLength = static_cast<float>(
                    1.0 / std::sqrt(lengthSquared));
                for (float& component : key.Value) component *= inverseLength;
            }
            else
            {
                // Translation/scale are vec3 channels. Canonical zero in the
                // unused fourth lane prevents editor history and serialization
                // from differing because of irrelevant garbage data.
                key.Value[3] = 0.0f;
                key.InTangent[3] = 0.0f;
                key.OutTangent[3] = 0.0f;
            }
            return key;
        }

        [[nodiscard]] inline bool TrackLess(
            const AnimationTimelineTrack& left,
            const AnimationTrackIdentity& right) noexcept
        {
            if (left.Identity.TargetNode != right.TargetNode)
                return left.Identity.TargetNode < right.TargetNode;
            return static_cast<std::uint8_t>(left.Identity.Path) <
                static_cast<std::uint8_t>(right.Path);
        }

        [[nodiscard]] inline bool IdentityLess(
            const AnimationTrackIdentity& left,
            const AnimationTimelineTrack& right) noexcept
        {
            if (left.TargetNode != right.Identity.TargetNode)
                return left.TargetNode < right.Identity.TargetNode;
            return static_cast<std::uint8_t>(left.Path) <
                static_cast<std::uint8_t>(right.Identity.Path);
        }
    }

    /// Value-semantic, renderer-independent animation authoring model.
    ///
    /// The timeline deliberately compiles to KairoAssets' glTF-compatible clip
    /// contract instead of defining a second runtime animation format. Editor
    /// history can snapshot this object directly; EngineCore's existing sampler,
    /// blend, skinning and root-motion paths consume the compiled result.
    class AnimationTimeline final
    {
    public:
        explicit AnimationTimeline(std::string name = "Animation")
            : m_Name(std::move(name))
        {
            SetName(m_Name);
        }

        [[nodiscard]] const std::string& Name() const noexcept { return m_Name; }

        void SetName(std::string name)
        {
            if (name.empty() || name.size() > 4096u)
                throw std::invalid_argument(
                    "Animation timeline name must contain 1 to 4096 bytes.");
            m_Name = std::move(name);
        }

        [[nodiscard]] const std::vector<AnimationTimelineTrack>& Tracks() const noexcept
        {
            return m_Tracks;
        }

        [[nodiscard]] std::size_t TrackCount() const noexcept
        {
            return m_Tracks.size();
        }

        [[nodiscard]] bool HasTrack(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path) const noexcept
        {
            return FindTrack({ targetNode, path }) != m_Tracks.end();
        }

        AnimationTimelineTrack& AddTrack(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path,
            kairo::assets::GltfAnimationInterpolation interpolation =
                kairo::assets::GltfAnimationInterpolation::Linear)
        {
            if (targetNode == kairo::assets::GltfMissingIndex)
                throw std::invalid_argument(
                    "Animation timeline track requires a concrete target node.");
            animation_timeline_detail::ValidatePath(path);
            animation_timeline_detail::ValidateInterpolation(interpolation);
            const AnimationTrackIdentity identity{ targetNode, path };
            const auto insertion = std::lower_bound(m_Tracks.begin(), m_Tracks.end(),
                identity, animation_timeline_detail::TrackLess);
            if (insertion != m_Tracks.end() && insertion->Identity == identity)
                throw std::invalid_argument(
                    "Animation timeline already contains this node/path track.");
            return *m_Tracks.insert(insertion,
                AnimationTimelineTrack{ identity, interpolation, {} });
        }

        bool RemoveTrack(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path)
        {
            const auto found = FindTrack({ targetNode, path });
            if (found == m_Tracks.end()) return false;
            m_Tracks.erase(found);
            return true;
        }

        AnimationTimelineTrack& Track(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path)
        {
            const auto found = FindTrack({ targetNode, path });
            if (found == m_Tracks.end())
                throw std::out_of_range("Animation timeline track does not exist.");
            return *found;
        }

        [[nodiscard]] const AnimationTimelineTrack& Track(
            std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path) const
        {
            const auto found = FindTrack({ targetNode, path });
            if (found == m_Tracks.end())
                throw std::out_of_range("Animation timeline track does not exist.");
            return *found;
        }

        void SetInterpolation(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path,
            kairo::assets::GltfAnimationInterpolation interpolation)
        {
            animation_timeline_detail::ValidateInterpolation(interpolation);
            Track(targetNode, path).Interpolation = interpolation;
        }

        /// Inserts or updates a key while keeping the track sorted by exact
        /// authored time. Replace is intended for property recording; Reject is
        /// the safer policy for timeline drags and paste operations.
        void UpsertKey(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path,
            AnimationTimelineKey key,
            AnimationKeyCollisionPolicy collision =
                AnimationKeyCollisionPolicy::Replace)
        {
            auto& track = Track(targetNode, path);
            key = animation_timeline_detail::CanonicalizeKey(path, std::move(key));
            const auto insertion = std::lower_bound(track.Keys.begin(), track.Keys.end(),
                key.TimeSeconds,
                [](const AnimationTimelineKey& candidate, float time)
                { return candidate.TimeSeconds < time; });
            if (insertion != track.Keys.end() &&
                insertion->TimeSeconds == key.TimeSeconds)
            {
                if (collision == AnimationKeyCollisionPolicy::Reject)
                    throw std::invalid_argument(
                        "Animation key time is already occupied.");
                if (collision != AnimationKeyCollisionPolicy::Replace)
                    throw std::invalid_argument(
                        "Animation key collision policy is invalid.");
                *insertion = std::move(key);
                return;
            }
            track.Keys.insert(insertion, std::move(key));
        }

        bool RemoveKey(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path, float timeSeconds)
        {
            if (!std::isfinite(timeSeconds) || timeSeconds < 0.0f)
                throw std::invalid_argument(
                    "Animation key time must be finite and non-negative.");
            auto& keys = Track(targetNode, path).Keys;
            const auto found = std::lower_bound(keys.begin(), keys.end(), timeSeconds,
                [](const AnimationTimelineKey& key, float time)
                { return key.TimeSeconds < time; });
            if (found == keys.end() || found->TimeSeconds != timeSeconds) return false;
            keys.erase(found);
            return true;
        }

        void MoveKey(std::uint32_t targetNode,
            kairo::assets::GltfAnimationPath path,
            float sourceTimeSeconds, float destinationTimeSeconds,
            AnimationKeyCollisionPolicy collision =
                AnimationKeyCollisionPolicy::Reject)
        {
            if (!std::isfinite(sourceTimeSeconds) || sourceTimeSeconds < 0.0f)
                throw std::invalid_argument("Animation source key time is invalid.");
            auto& track = Track(targetNode, path);
            const auto source = std::lower_bound(track.Keys.begin(), track.Keys.end(),
                sourceTimeSeconds,
                [](const AnimationTimelineKey& key, float time)
                { return key.TimeSeconds < time; });
            if (source == track.Keys.end() || source->TimeSeconds != sourceTimeSeconds)
                throw std::out_of_range("Animation source key does not exist.");
            AnimationTimelineKey moved = *source;
            if (sourceTimeSeconds == destinationTimeSeconds) return;
            track.Keys.erase(source);
            moved.TimeSeconds = destinationTimeSeconds;
            try
            {
                UpsertKey(targetNode, path, moved, collision);
            }
            catch (...)
            {
                // Transactional editor semantics: a failed drag/paste restores
                // the exact source key rather than losing authored data.
                UpsertKey(targetNode, path, moved = AnimationTimelineKey{
                    sourceTimeSeconds, moved.Value, moved.InTangent, moved.OutTangent },
                    AnimationKeyCollisionPolicy::Reject);
                throw;
            }
        }

        [[nodiscard]] float DurationSeconds() const noexcept
        {
            float duration = 0.0f;
            for (const auto& track : m_Tracks)
                if (!track.Keys.empty())
                    duration = std::max(duration, track.Keys.back().TimeSeconds);
            return duration;
        }

        void Validate() const
        {
            if (m_Name.empty() || m_Name.size() > 4096u)
                throw std::invalid_argument("Animation timeline name is invalid.");
            AnimationTrackIdentity previous{};
            bool havePrevious = false;
            for (const auto& track : m_Tracks)
            {
                if (track.Identity.TargetNode == kairo::assets::GltfMissingIndex)
                    throw std::invalid_argument(
                        "Animation timeline track target is invalid.");
                animation_timeline_detail::ValidatePath(track.Identity.Path);
                animation_timeline_detail::ValidateInterpolation(track.Interpolation);
                if (havePrevious && !animation_timeline_detail::IdentityLess(
                        previous, track))
                    throw std::invalid_argument(
                        "Animation timeline tracks are duplicated or not canonicalized.");
                previous = track.Identity;
                havePrevious = true;

                float previousTime = -1.0f;
                for (const auto& key : track.Keys)
                {
                    const auto canonical =
                        animation_timeline_detail::CanonicalizeKey(
                            track.Identity.Path, key);
                    if (canonical != key)
                        throw std::invalid_argument(
                            "Animation timeline contains a non-canonical key.");
                    if (key.TimeSeconds <= previousTime)
                        throw std::invalid_argument(
                            "Animation timeline key times must be strictly increasing.");
                    previousTime = key.TimeSeconds;
                }
            }
        }

        [[nodiscard]] kairo::assets::GltfAnimationClipData Compile() const
        {
            Validate();
            kairo::assets::GltfAnimationClipData clip;
            clip.Name = m_Name;
            clip.Channels.reserve(m_Tracks.size());
            for (const auto& track : m_Tracks)
            {
                // Empty authoring tracks are useful while editing, but have no
                // runtime meaning and are intentionally omitted from the clip.
                if (track.Keys.empty()) continue;
                kairo::assets::GltfAnimationChannelData channel;
                channel.TargetNode = track.Identity.TargetNode;
                channel.Path = track.Identity.Path;
                channel.Interpolation = track.Interpolation;
                channel.Keyframes.reserve(track.Keys.size());
                for (const auto& key : track.Keys)
                    channel.Keyframes.push_back({ key.TimeSeconds, key.Value,
                        key.InTangent, key.OutTangent });
                clip.Channels.push_back(std::move(channel));
            }
            return clip;
        }

        [[nodiscard]] static AnimationTimeline FromClip(
            const kairo::assets::GltfAnimationClipData& clip)
        {
            AnimationTimeline timeline(clip.Name.empty() ? "Animation" : clip.Name);
            for (const auto& channel : clip.Channels)
            {
                auto& track = timeline.AddTrack(channel.TargetNode,
                    channel.Path, channel.Interpolation);
                (void)track;
                for (const auto& key : channel.Keyframes)
                    timeline.UpsertKey(channel.TargetNode, channel.Path,
                        { key.TimeSeconds, key.Value,
                            key.InTangent, key.OutTangent },
                        AnimationKeyCollisionPolicy::Reject);
            }
            timeline.Validate();
            return timeline;
        }

        friend bool operator==(const AnimationTimeline&,
            const AnimationTimeline&) = default;

    private:
        std::string m_Name;
        std::vector<AnimationTimelineTrack> m_Tracks;

        using TrackIterator = std::vector<AnimationTimelineTrack>::iterator;
        using ConstTrackIterator =
            std::vector<AnimationTimelineTrack>::const_iterator;

        [[nodiscard]] TrackIterator FindTrack(
            const AnimationTrackIdentity& identity) noexcept
        {
            const auto found = std::lower_bound(m_Tracks.begin(), m_Tracks.end(),
                identity, animation_timeline_detail::TrackLess);
            return found != m_Tracks.end() && found->Identity == identity
                ? found : m_Tracks.end();
        }

        [[nodiscard]] ConstTrackIterator FindTrack(
            const AnimationTrackIdentity& identity) const noexcept
        {
            const auto found = std::lower_bound(m_Tracks.begin(), m_Tracks.end(),
                identity, animation_timeline_detail::TrackLess);
            return found != m_Tracks.end() && found->Identity == identity
                ? found : m_Tracks.end();
        }
    };
}
