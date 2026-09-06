module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <imgui.h>

export module Kairo.Editor.AnimationPreview;

import Kairo.Editor.SceneRenderBridge;
import Kairo.EngineCore;

export namespace kairo::editor
{
    struct AnimationPreviewState final
    {
        bool Enabled = false;
        bool Playing = true;
        bool Loop = true;
        std::uint32_t ClipIndex = 0u;
        float TimeSeconds = 0.0f;
        float Speed = 1.0f;
    };

    /// Editor-only controller for non-destructive glTF animation preview.
    /// Playback lives outside serialized scene data: selecting/scrubbing a clip
    /// changes renderer extraction only and never mutates authored transforms.
    class AnimationPreviewController final
    {
    public:
        [[nodiscard]] const SceneAnimationOverrides& Overrides() const noexcept
        {
            return m_Overrides;
        }

        void Clear(kairo::engine::Entity entity) noexcept
        {
            m_Overrides.Clear(entity);
            m_State.erase(entity.Value);
        }

        void Draw(std::optional<kairo::engine::Entity> selected,
            const kairo::engine::Scene& scene,
            const RenderAssetBindings& assets)
        {
            ImGui::Begin("Animation Preview");

            if (!selected.has_value() || !scene.Contains(*selected))
            {
                ImGui::TextDisabled("Select an imported glTF scene instance.");
                ImGui::End();
                return;
            }

            const auto entity = *selected;
            if (!scene.HasSceneInstance(entity))
            {
                ImGui::TextDisabled("Selected entity is not a scene instance.");
                ImGui::End();
                return;
            }

            const auto& instance = scene.SceneInstance(entity);
            const auto* gltf = assets.ResolveGltfSource(instance.SceneAsset);
            if (gltf == nullptr)
            {
                m_Overrides.Clear(entity);
                ImGui::TextDisabled("This scene binding has no glTF animation source.");
                ImGui::End();
                return;
            }
            if (gltf->Animations.empty())
            {
                m_Overrides.Clear(entity);
                ImGui::TextDisabled("This glTF scene contains no animation clips.");
                ImGui::End();
                return;
            }

            auto& state = m_State[entity.Value];
            state.ClipIndex = std::min<std::uint32_t>(state.ClipIndex,
                static_cast<std::uint32_t>(gltf->Animations.size() - 1u));

            if (ImGui::Checkbox("Preview", &state.Enabled) && !state.Enabled)
                m_Overrides.Clear(entity);

            const auto clipLabel = [&](std::uint32_t index)
            {
                const auto& clip = gltf->Animations[index];
                return clip.Name.empty()
                    ? std::string("Clip ") + std::to_string(index)
                    : clip.Name;
            };

            std::string currentLabel = clipLabel(state.ClipIndex);
            if (ImGui::BeginCombo("Clip", currentLabel.c_str()))
            {
                for (std::uint32_t index = 0u;
                    index < static_cast<std::uint32_t>(gltf->Animations.size()); ++index)
                {
                    const bool current = index == state.ClipIndex;
                    const std::string label = clipLabel(index);
                    if (ImGui::Selectable(label.c_str(), current))
                    {
                        state.ClipIndex = index;
                        state.TimeSeconds = 0.0f;
                    }
                    if (current) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const auto& clip = gltf->Animations[state.ClipIndex];
            const float duration = std::max(0.0f, clip.DurationSeconds());

            if (ImGui::Button(state.Playing ? "Pause" : "Play"))
                state.Playing = !state.Playing;
            ImGui::SameLine();
            if (ImGui::Button("Reset")) state.TimeSeconds = 0.0f;
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &state.Loop);

            ImGui::SetNextItemWidth(180.0f);
            ImGui::DragFloat("Speed", &state.Speed, 0.05f, 0.0f, 4.0f, "%.2fx");
            state.Speed = std::clamp(state.Speed, 0.0f, 4.0f);

            if (duration > 0.0f)
            {
                if (state.Enabled && state.Playing)
                {
                    state.TimeSeconds += ImGui::GetIO().DeltaTime * state.Speed;
                    if (state.Loop)
                    {
                        state.TimeSeconds = std::fmod(state.TimeSeconds, duration);
                        if (state.TimeSeconds < 0.0f) state.TimeSeconds += duration;
                    }
                    else
                    {
                        state.TimeSeconds = std::clamp(state.TimeSeconds, 0.0f, duration);
                        if (state.TimeSeconds >= duration) state.Playing = false;
                    }
                }
                ImGui::SliderFloat("Time", &state.TimeSeconds, 0.0f, duration, "%.3f s");
            }
            else
            {
                state.TimeSeconds = 0.0f;
                ImGui::TextDisabled("Duration: 0 s");
            }

            ImGui::TextDisabled("%zu channel%s | %.3f s",
                clip.Channels.size(), clip.Channels.size() == 1u ? "" : "s", duration);

            if (state.Enabled)
            {
                m_Overrides.Set(entity, {
                    .ClipIndex = state.ClipIndex,
                    .TimeSeconds = state.TimeSeconds,
                    .TimeMode = state.Loop
                        ? kairo::engine::AnimationTimeMode::Loop
                        : kairo::engine::AnimationTimeMode::Clamp });
            }
            else
            {
                m_Overrides.Clear(entity);
            }

            ImGui::End();
        }

    private:
        std::unordered_map<std::uint32_t, AnimationPreviewState> m_State;
        SceneAnimationOverrides m_Overrides;
    };
}
