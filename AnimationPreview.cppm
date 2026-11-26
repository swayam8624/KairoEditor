module;

#include "detail/AnimationTimeline.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <imgui.h>

export module Kairo.Editor.AnimationPreview;

import Kairo.Runtime.RealtimeSceneBridge;
import Kairo.EngineCore;
import Kairo.Assets;

export namespace kairo::editor
{
    struct AnimationPreviewState final
    {
        bool Enabled = false;
        bool Playing = false;
        bool Loop = true;
        std::uint32_t ClipIndex = 0u;
        std::size_t TrackIndex = 0u;
        std::size_t FirstTrack = 0u;
        float TimeSeconds = 0.0f;
        float Speed = 1.0f;
    };

    // Editor-only inspection of imported animation tracks. The active
    // override belongs to exactly one entity and one scene asset. It never
    // writes source animation data or the authored scene.
    class AnimationPreviewController final
    {
    public:
        [[nodiscard]] const kairo::runtime::renderbridge::SceneAnimationOverrides& Overrides() const noexcept
        {
            return m_Overrides;
        }

        void Clear(kairo::engine::Entity entity) noexcept
        {
            m_Overrides.Clear(entity);
            if(m_Entity&&*m_Entity==entity)
            {
                m_Entity.reset();
                m_Asset.reset();
                m_State={};
            }
        }

        void Draw(std::optional<kairo::engine::Entity> selected,
            const kairo::engine::Scene& scene,
            const kairo::runtime::renderbridge::RenderAssetBindings& assets)
        {
            if(!selected||!scene.Contains(*selected)||!scene.HasSceneInstance(*selected))
                selected.reset();
            if(m_Entity&&m_Entity!=selected)Clear(*m_Entity);

            const bool visible=ImGui::Begin("Timeline");
            if(!selected)
            {
                if(visible)ImGui::TextDisabled("Select an imported glTF scene instance.");
                ImGui::End();
                return;
            }
            const auto entity=*selected;
            const auto& instance=scene.SceneInstance(entity);
            if(!m_Entity||!m_Asset||*m_Asset!=instance.SceneAsset.ID)
            {
                if(m_Entity)Clear(*m_Entity);
                m_Entity=entity;
                m_Asset=instance.SceneAsset.ID;
            }
            const auto* gltf=assets.ResolveGltfSource(instance.SceneAsset);
            if(!gltf||gltf->Animations.empty())
            {
                m_Overrides.Clear(entity);
                m_State={};
                if(visible)ImGui::TextDisabled("No imported animation clips on this instance.");
                ImGui::End();
                return;
            }
            if(!visible)
            {
                m_State.Playing=false;
                m_Overrides.Clear(entity);
                ImGui::End();
                return;
            }

            auto& state=m_State;
            state.ClipIndex=std::min<std::uint32_t>(state.ClipIndex,
                static_cast<std::uint32_t>(gltf->Animations.size()-1u));
            if(ImGui::Checkbox("Preview",&state.Enabled)&&!state.Enabled)state.Playing=false;
            ImGui::SameLine();
            ImGui::TextDisabled("Imported clip | source read-only");

            const auto clipLabel=[&](std::uint32_t index)
            {
                const auto& clip=gltf->Animations[index];
                return clip.Name.empty()?std::string("Clip ")+std::to_string(index):clip.Name;
            };
            const auto label=clipLabel(state.ClipIndex);
            ImGui::SetNextItemWidth(220.f);
            if(ImGui::BeginCombo("Clip",label.c_str()))
            {
                // Clip selection is indexed, even if names are duplicated.
                for(std::uint32_t i=0;i<gltf->Animations.size();++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const bool current=i==state.ClipIndex;
                    if(ImGui::Selectable(clipLabel(i).c_str(),current))
                    {
                        state.ClipIndex=i;
                        state.TimeSeconds=0;
                        state.TrackIndex=0;
                        state.FirstTrack=0;
                        state.Playing=false;
                    }
                    if(current)ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            const auto& clip=gltf->Animations[state.ClipIndex];
            const float duration=std::max(0.f,clip.DurationSeconds());
            if(ImGui::Button(state.Playing?"Pause":"Play"))
            {
                state.Enabled=true;
                state.Playing=!state.Playing;
                if(state.Playing&&state.TimeSeconds>=duration)state.TimeSeconds=0;
            }
            ImGui::SameLine();
            if(ImGui::Button("Stop")){state.TimeSeconds=0;state.Playing=false;}
            ImGui::SameLine();
            ImGui::Checkbox("Loop",&state.Loop);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.f);
            ImGui::DragFloat("Speed",&state.Speed,.05f,0.f,4.f,"%.2fx");
            if(!std::isfinite(state.Speed))state.Speed=1.f;
            state.Speed=std::clamp(state.Speed,0.f,4.f);
            const auto clock=detail::AdvanceTimeline({state.TimeSeconds,state.Playing&&state.Enabled},
                duration,ImGui::GetIO().DeltaTime,state.Speed,state.Loop);
            state.TimeSeconds=clock.Time;
            state.Playing=clock.Playing;
            float fraction=duration>0?state.TimeSeconds/duration:0;
            if(duration>0&&ImGui::SliderFloat("Scrub",&fraction,0,1,"%.3f"))
                Seek(detail::TimelineTimeAt(fraction,duration));
            ImGui::TextDisabled("%zu tracks | %.3f / %.3f s",
                clip.Channels.size(),state.TimeSeconds,duration);

            if(!clip.Channels.empty())
            {
                state.TrackIndex=std::min(state.TrackIndex,clip.Channels.size()-1);
                DrawTracks(clip,*gltf,duration);
            }
            if(state.Enabled)
                m_Overrides.Set(entity,{
                    .ClipIndex=state.ClipIndex,
                    .TimeSeconds=state.TimeSeconds,
                    // Clock already wraps. Clamp samples the exact last key.
                    .TimeMode=kairo::engine::AnimationTimeMode::Clamp});
            else m_Overrides.Clear(entity);
            ImGui::End();
        }

    private:
        static constexpr std::size_t TracksPerPage=12;
        std::optional<kairo::engine::Entity> m_Entity;
        std::optional<kairo::assets::AssetID> m_Asset;
        AnimationPreviewState m_State;
        kairo::runtime::renderbridge::SceneAnimationOverrides m_Overrides;

        static const char* PathName(kairo::assets::GltfAnimationPath path) noexcept
        {
            using enum kairo::assets::GltfAnimationPath;
            switch(path)
            {
                case Translation:return "Position";
                case Rotation:return "Rotation";
                case Scale:return "Scale";
                default:return "Unknown";
            }
        }

        static const char* InterpolationName(kairo::assets::GltfAnimationInterpolation mode) noexcept
        {
            using enum kairo::assets::GltfAnimationInterpolation;
            switch(mode)
            {
                case Linear:return "Linear";
                case Step:return "Step";
                case CubicSpline:return "Cubic spline";
                default:return "Unknown";
            }
        }

        void Seek(float time) noexcept
        {
            m_State.TimeSeconds=time;
            m_State.Playing=false;
            m_State.Enabled=true;
        }

        void DrawTracks(const kairo::assets::GltfAnimationClipData& clip,
            const kairo::assets::GltfSceneArtifactData& source,float duration)
        {
            auto& state=m_State;
            const auto selectedKeys=std::span<const kairo::assets::GltfAnimationKeyframe>(
                clip.Channels[state.TrackIndex].Keyframes);
            const auto previous=detail::AdjacentKey(selectedKeys,state.TimeSeconds,false);
            const auto next=detail::AdjacentKey(selectedKeys,state.TimeSeconds,true);
            ImGui::BeginDisabled(!previous);
            if(ImGui::Button("Previous key")&&previous)Seek(*previous);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!next);
            if(ImGui::Button("Next key")&&next)Seek(*next);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Last frame"))Seek(duration);

            const std::size_t count=clip.Channels.size();
            state.FirstTrack=std::min(state.FirstTrack,((count-1)/TracksPerPage)*TracksPerPage);
            ImGui::BeginDisabled(state.FirstTrack==0);
            if(ImGui::Button("Previous tracks"))state.FirstTrack-=TracksPerPage;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(state.FirstTrack+TracksPerPage>=count);
            if(ImGui::Button("Next tracks"))state.FirstTrack+=TracksPerPage;
            ImGui::EndDisabled();
            const auto last=std::min(count,state.FirstTrack+TracksPerPage);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu-%zu / %zu",state.FirstTrack+1,last,count);

            if(ImGui::BeginTable("##animation_tracks",2,
                ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Track",ImGuiTableColumnFlags_WidthFixed,190.f);
                ImGui::TableSetupColumn("Time / keys",ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for(std::size_t i=state.FirstTrack;i<last;++i)
                {
                    const auto& channel=clip.Channels[i];
                    const auto keys=std::span<const kairo::assets::GltfAnimationKeyframe>(channel.Keyframes);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const auto node=channel.TargetNode<source.Nodes.size()
                        ?source.Nodes[channel.TargetNode].Name:std::string{};
                    const auto title=(node.empty()?"Node "+std::to_string(channel.TargetNode):node)
                        +" / "+PathName(channel.Path);
                    if(ImGui::Selectable(title.c_str(),i==state.TrackIndex))
                        state.TrackIndex=i;
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s | %zu keys | %s",
                        title.c_str(),keys.size(),InterpolationName(channel.Interpolation));
                    ImGui::TableSetColumnIndex(1);
                    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
                    constexpr float height=20.f;
                    const ImVec2 p=ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##track",ImVec2(width,height));
                    if(ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        state.TrackIndex=i;
                        Seek(detail::TimelineTimeAt((ImGui::GetIO().MousePos.x-p.x)/width,duration));
                    }
                    auto* draw=ImGui::GetWindowDrawList();
                    const ImU32 grid=ImGui::GetColorU32(ImGuiCol_Border);
                    const ImU32 accent=ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
                    const ImU32 keyColor=ImGui::GetColorU32(i==state.TrackIndex?ImGuiCol_SliderGrabActive:ImGuiCol_TextDisabled);
                    draw->PushClipRect(p,ImVec2(p.x+width,p.y+height),true);
                    for(unsigned j=0;j<=4;++j)
                    {
                        const float x=p.x+width*float(j)/4.f;
                        draw->AddLine(ImVec2(x,p.y),ImVec2(x,p.y+height),grid);
                    }
                    // At most 256 binary-search bins per visible track. Dense
                    // data shares markers rather than producing unbounded draw calls.
                    const unsigned bins=static_cast<unsigned>(std::clamp(width/4.f,1.f,256.f));
                    for(unsigned j=0;j<bins;++j)
                    {
                        const float a=duration*(float(j)/float(bins));
                        const float b=duration*(float(j+1)/float(bins));
                        if(!detail::HasKeyInBin(keys,a,b,j+1==bins))continue;
                        const float x=p.x+width*(float(j)+.5f)/float(bins);
                        const float y=p.y+height*.5f;
                        draw->AddQuadFilled(ImVec2(x,y-3),ImVec2(x+3,y),
                            ImVec2(x,y+3),ImVec2(x-3,y),keyColor);
                    }
                    const float playhead=p.x+(duration>0?state.TimeSeconds/duration:0)*width;
                    draw->AddLine(ImVec2(playhead,p.y),ImVec2(playhead,p.y+height),accent,2.f);
                    draw->PopClipRect();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            const auto& active=clip.Channels[state.TrackIndex];
            ImGui::TextDisabled("%s | %s | %zu keys",
                PathName(active.Path),InterpolationName(active.Interpolation),active.Keyframes.size());
            // Inspect the authored key nearest at or before the playhead.
            // Rotation is the source quaternion (x,y,z,w), not Euler angles.
            const auto after=std::upper_bound(active.Keyframes.begin(),active.Keyframes.end(),state.TimeSeconds,
                [](float t,const auto& key){return t<key.TimeSeconds;});
            if(!active.Keyframes.empty())
            {
                const auto& key=after==active.Keyframes.begin()?*after:*(after-1);
                ImGui::Text("Source key %.3f s | %zu of %zu",key.TimeSeconds,
                    static_cast<std::size_t>(&key-active.Keyframes.data())+1,active.Keyframes.size());
                ImGui::SameLine();
                if(ImGui::SmallButton("Jump to key"))Seek(key.TimeSeconds);
                if(active.Path==kairo::assets::GltfAnimationPath::Rotation)
                    ImGui::Text("Quaternion: %.4f, %.4f, %.4f, %.4f",
                        key.Value[0],key.Value[1],key.Value[2],key.Value[3]);
                else
                    ImGui::Text("XYZ: %.4f, %.4f, %.4f",key.Value[0],key.Value[1],key.Value[2]);
            }
        }
    };
}
