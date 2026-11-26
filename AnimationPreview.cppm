module;

#include "detail/AnimationTimeline.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <exception>
#include <stdexcept>
#include <vector>
#include <variant>
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
                m_Draft.reset();
                m_ControllerDraft.reset();
                m_ControllerPreview.reset();
                m_Parameters.Clear();
            }
        }

        void Draw(std::optional<kairo::engine::Entity> selected,
            const kairo::engine::Scene& scene,
            kairo::runtime::renderbridge::RenderAssetBindings& assets,
            const std::filesystem::path& root,bool canEdit)
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
            if(!gltf)
            {
                m_Overrides.Clear(entity);
                m_State={};
                if(visible)ImGui::TextDisabled("No glTF source on this instance.");
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

            DrawAuthoring(assets,instance.SceneAsset,*gltf,root,canEdit);
            if(gltf->Animations.empty())
            {
                m_Overrides.Clear(entity);
                ImGui::TextDisabled("Create a project animation to begin.");
                ImGui::End();
                return;
            }
            DrawController(assets,instance.SceneAsset,*gltf,root,canEdit);
            auto& state=m_State;
            state.ClipIndex=std::min<std::uint32_t>(state.ClipIndex,
                static_cast<std::uint32_t>(gltf->Animations.size()-1u));
            if(ImGui::Checkbox("Preview",&state.Enabled)&&!state.Enabled)state.Playing=false;
            ImGui::SameLine();
            const bool authored=assets.HasAuthoredClip(instance.SceneAsset)&&
                state.ClipIndex==gltf->Animations.size()-1;
            ImGui::TextDisabled("%s",authored?"Project-authored clip":"Imported clip | source read-only");

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
            if(m_ControllerPreview)
            {
                try
                {
                    m_Overrides.SetPose(entity,instance.SceneAsset.ID,m_ControllerPreview->Advance(*gltf,m_Parameters,
                        std::clamp(ImGui::GetIO().DeltaTime,0.f,1.f)));
                }
                catch(const std::exception& e){m_ControllerError=e.what();m_ControllerPreview.reset();m_Overrides.Clear(entity);}
            }
            else if(state.Enabled)
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
        std::optional<kairo::assets::GltfAnimationClipData> m_Draft;
        std::optional<kairo::engine::AnimationControllerDefinition> m_ControllerDraft;
        std::optional<kairo::engine::AnimationControllerRuntime> m_ControllerPreview;
        kairo::engine::AnimationParameterSet m_Parameters;
        std::string m_ControllerError;
        int m_ControllerState=0,m_ControllerTransition=0;
        std::array<char,129> m_DraftName{};
        std::string m_DraftError;
        int m_EditTrack=0,m_EditKey=0,m_NewNode=0,m_NewPath=0;
        kairo::runtime::renderbridge::SceneAnimationOverrides m_Overrides;

        // Draft edits never touch the bound asset until explicit Save. A
        // modal owns the draft so selection and ordinary scene editing cannot
        // accidentally switch its target during authoring.
        void DrawAuthoring(kairo::runtime::renderbridge::RenderAssetBindings& assets,
            kairo::assets::SceneAssetHandle asset,const kairo::assets::GltfSceneArtifactData& source,
            const std::filesystem::path& root,bool canEdit)
        {
            using namespace kairo::assets;
            const bool existing=assets.HasAuthoredClip(asset);
            ImGui::BeginDisabled(!canEdit||!assets.CanAuthorClip(asset));
            if(ImGui::Button(existing?"Edit project clip":"Create project clip"))
            {
                m_Draft=GltfAnimationClipData{};
                m_DraftName.fill(0);
                m_DraftError.clear();
                m_EditTrack=m_EditKey=m_NewNode=m_NewPath=0;
                if(existing&&!source.Animations.empty())
                    *m_Draft=source.Animations.back();
                else
                    m_Draft->Name="New animation";
                const auto n=std::min(m_Draft->Name.size(),m_DraftName.size()-1);
                std::copy_n(m_Draft->Name.begin(),n,m_DraftName.begin());
                ImGui::OpenPopup("Project animation");
            }
            ImGui::EndDisabled();
            if(!ImGui::BeginPopupModal("Project animation",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
            if(!m_Draft){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
            auto& draft=*m_Draft;
            ImGui::TextWrapped("One project-authored clip per asset. Changes affect all instances using this asset. Imported clips stay unchanged.");
            ImGui::InputText("Name",m_DraftName.data(),m_DraftName.size());
            if(!existing&&draft.Channels.empty()&&!source.Animations.empty())
            {
                if(ImGui::Button("Copy selected imported clip"))
                {
                    try
                    {
                        const auto& original=source.Animations.at(std::min<std::size_t>(
                            m_State.ClipIndex,source.Animations.size()-1));
                        std::size_t total=0;
                        if(original.Channels.size()>256)throw std::length_error("Too many source tracks.");
                        for(const auto& c:original.Channels)
                        {
                            if(c.Keyframes.size()>32768-total)throw std::length_error("Too many source keys.");
                            total+=c.Keyframes.size();
                        }
                        draft.Channels=original.Channels;
                        m_DraftError.clear();
                    }
                    catch(const std::exception& e){m_DraftError=e.what();}
                }
            }
            ImGui::Separator();
            ImGui::InputInt("Node index",&m_NewNode);
            if(m_NewNode>=0&&static_cast<std::size_t>(m_NewNode)<source.Nodes.size())
            {
                const auto& node=source.Nodes[static_cast<std::size_t>(m_NewNode)];
                ImGui::TextDisabled("%s%s",node.Name.c_str(),node.HasRestTRS?"":" (matrix-only)");
            }
            constexpr const char* paths[]={"Position","Rotation","Scale"};
            ImGui::Combo("Property",&m_NewPath,paths,3);
            if(ImGui::Button("Add track"))
            {
                try
                {
                    if(m_NewNode<0||static_cast<std::size_t>(m_NewNode)>=source.Nodes.size()||
                        !source.Nodes[static_cast<std::size_t>(m_NewNode)].HasRestTRS)
                        throw std::invalid_argument("Select a node with rest TRS.");
                    const auto path=static_cast<GltfAnimationPath>(m_NewPath+1);
                    if(draft.Channels.size()>=256||std::any_of(draft.Channels.begin(),draft.Channels.end(),
                        [&](const auto& c){return c.TargetNode==static_cast<std::uint32_t>(m_NewNode)&&c.Path==path;}))
                        throw std::invalid_argument("Track already exists or track budget exceeded.");
                    const auto& node=source.Nodes[static_cast<std::size_t>(m_NewNode)];
                    GltfAnimationChannelData channel;
                    channel.TargetNode=static_cast<std::uint32_t>(m_NewNode);
                    channel.Path=path;
                    GltfAnimationKeyframe key;
                    if(path==GltfAnimationPath::Rotation)key.Value=node.RestRotation;
                    else
                    {
                        const auto& v=path==GltfAnimationPath::Translation?node.RestTranslation:node.RestScale;
                        key.Value={v[0],v[1],v[2],0};
                    }
                    std::size_t total=0;
                    for(const auto& c:draft.Channels)total+=c.Keyframes.size();
                    if(total>=32768)throw std::length_error("Key budget exceeded.");
                    channel.Keyframes.push_back(key);
                    draft.Channels.push_back(std::move(channel));
                    m_EditTrack=static_cast<int>(draft.Channels.size()-1);m_EditKey=0;
                    m_DraftError.clear();
                }
                catch(const std::exception& e){m_DraftError=e.what();}
            }
            if(!draft.Channels.empty())
            {
                m_EditTrack=std::clamp(m_EditTrack,0,static_cast<int>(draft.Channels.size()-1));
                if(ImGui::InputInt("Track index",&m_EditTrack))
                {
                    m_EditTrack=std::clamp(m_EditTrack,0,static_cast<int>(draft.Channels.size()-1));
                    m_EditKey=0;
                }
                auto& channel=draft.Channels[static_cast<std::size_t>(m_EditTrack)];
                ImGui::Text("Track %d / %zu | Node %u | %s",m_EditTrack+1,draft.Channels.size(),
                    channel.TargetNode,PathName(channel.Path));
                int interpolation=static_cast<int>(channel.Interpolation)-1;
                constexpr const char* modes[]={"Linear","Step","Cubic spline"};
                if(ImGui::Combo("Interpolation",&interpolation,modes,3))
                    channel.Interpolation=static_cast<GltfAnimationInterpolation>(interpolation+1);
                m_EditKey=std::clamp(m_EditKey,0,static_cast<int>(channel.Keyframes.size()-1));
                if(ImGui::InputInt("Key index",&m_EditKey))
                    m_EditKey=std::clamp(m_EditKey,0,static_cast<int>(channel.Keyframes.size()-1));
                auto& key=channel.Keyframes[static_cast<std::size_t>(m_EditKey)];
                ImGui::Text("Key %d / %zu",m_EditKey+1,channel.Keyframes.size());
                ImGui::InputFloat("Seconds",&key.TimeSeconds);
                if(channel.Path==GltfAnimationPath::Rotation)
                {
                    ImGui::InputFloat4("Quaternion XYZW",key.Value.data());
                    if(ImGui::Button("Normalize rotation"))
                    {
                        double norm=0;
                        for(float v:key.Value)norm+=double(v)*v;
                        if(std::isfinite(norm)&&norm>0)
                            for(float& v:key.Value)v=static_cast<float>(v/std::sqrt(norm));
                        else m_DraftError="Rotation must be finite and nonzero.";
                    }
                }
                else ImGui::InputFloat3("XYZ",key.Value.data());
                if(channel.Interpolation==GltfAnimationInterpolation::CubicSpline)
                {
                    ImGui::InputFloat4("In tangent",key.InTangent.data());
                    ImGui::InputFloat4("Out tangent",key.OutTangent.data());
                }
                if(ImGui::Button("Duplicate key"))
                {
                    std::size_t total=0;
                    for(const auto& c:draft.Channels)total+=c.Keyframes.size();
                    if(total>=32768)m_DraftError="Key budget exceeded.";
                    else
                    {
                        auto copy=key;
                        copy.TimeSeconds=channel.Keyframes.back().TimeSeconds+1.f;
                        channel.Keyframes.push_back(copy);
                        m_EditKey=static_cast<int>(channel.Keyframes.size()-1);
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(channel.Keyframes.size()<=1);
                if(ImGui::Button("Delete key"))
                {
                    channel.Keyframes.erase(channel.Keyframes.begin()+m_EditKey);
                    m_EditKey=0;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if(ImGui::Button("Sort key times"))
                {
                    // A non-finite comparison is not a strict weak ordering.
                    if(std::all_of(channel.Keyframes.begin(),channel.Keyframes.end(),
                        [](const auto& k){return std::isfinite(k.TimeSeconds);}))
                    {
                        std::sort(channel.Keyframes.begin(),channel.Keyframes.end(),
                            [](const auto& a,const auto& b){return a.TimeSeconds<b.TimeSeconds;});
                        m_EditKey=0;
                    }
                    else m_DraftError="All key times must be finite.";
                }
                if(ImGui::Button("Delete track"))
                {
                    draft.Channels.erase(draft.Channels.begin()+m_EditTrack);
                    m_EditTrack=m_EditKey=0;
                }
            }
            if(!m_DraftError.empty())ImGui::TextWrapped("%s",m_DraftError.c_str());
            ImGui::BeginDisabled(!canEdit||draft.Channels.empty());
            if(ImGui::Button("Save project clip"))
            {
                try
                {
                    draft.Name=m_DraftName.data();
                    const auto index=assets.CommitAuthoredClip(root,asset,draft);
                    m_State.ClipIndex=static_cast<std::uint32_t>(index);
                    m_State.TrackIndex=m_State.FirstTrack=0;
                    Seek(0);
                    m_Draft.reset();
                    m_ControllerPreview.reset();
                    ImGui::CloseCurrentPopup();
                }
                catch(const std::exception& e){m_DraftError=e.what();}
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Cancel draft"))
            {
                m_Draft.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        static void EditControllerName(const char* label,std::string& value)
        {
            std::array<char,129> buffer{};
            std::copy_n(value.begin(),std::min(value.size(),buffer.size()-1),buffer.begin());
            if(ImGui::InputText(label,buffer.data(),buffer.size()))value=buffer.data();
        }

        void DrawController(kairo::runtime::renderbridge::RenderAssetBindings& assets,
            kairo::assets::SceneAssetHandle asset,const kairo::assets::GltfSceneArtifactData& source,
            const std::filesystem::path& root,bool canEdit)
        {
            using namespace kairo::engine;
            const auto* definition=assets.ResolveController(asset);
            ImGui::BeginDisabled(!canEdit||!assets.CanAuthorClip(asset));
            if(ImGui::Button(definition?"Edit state machine":"Create state machine"))
            {
                m_ControllerDraft=definition?*definition:AnimationControllerDefinition{};
                if(!definition)m_ControllerDraft->States.push_back({"Entry",0,1,AnimationTimeMode::Loop});
                m_ControllerState=m_ControllerTransition=0;
                m_ControllerError.clear();
                m_ControllerPreview.reset();
                ImGui::OpenPopup("Animation state machine");
            }
            ImGui::EndDisabled();
            if(ImGui::BeginPopupModal("Animation state machine",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                if(!m_ControllerDraft){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
                auto& draft=*m_ControllerDraft;
                ImGui::TextWrapped("Asset-wide state machine. Conditions are optional boolean equality; parameters start false.");
                auto indexField=[](const char* label,std::size_t& value,std::size_t count)
                {
                    int v=static_cast<int>(value);
                    if(ImGui::InputInt(label,&v))value=static_cast<std::size_t>(std::clamp(v,0,static_cast<int>(count-1)));
                };
                indexField("Entry state",draft.EntryState,draft.States.size());
                ImGui::BeginDisabled(draft.States.size()>=64);
                if(ImGui::Button("Add state"))
                {
                    draft.States.push_back({"State "+std::to_string(draft.States.size()),0,1,AnimationTimeMode::Loop});
                    m_ControllerState=static_cast<int>(draft.States.size()-1);
                }
                ImGui::EndDisabled();
                ImGui::InputInt("State index",&m_ControllerState);
                m_ControllerState=std::clamp(m_ControllerState,0,static_cast<int>(draft.States.size()-1));
                auto& state=draft.States[static_cast<std::size_t>(m_ControllerState)];
                EditControllerName("State name",state.Name);
                indexField("Clip index",state.ClipIndex,source.Animations.size());
                ImGui::TextDisabled("Clip: %s",source.Animations[state.ClipIndex].Name.c_str());
                ImGui::InputFloat("State speed",&state.Speed);
                bool loop=state.TimeMode==AnimationTimeMode::Loop;
                if(ImGui::Checkbox("State loops",&loop))state.TimeMode=loop?AnimationTimeMode::Loop:AnimationTimeMode::Clamp;
                ImGui::BeginDisabled(draft.States.size()<2);
                if(ImGui::Button("Delete state"))
                {
                    const auto removed=static_cast<std::size_t>(m_ControllerState);
                    // Remove incident transitions and repair surviving indices.
                    std::erase_if(draft.Transitions,[&](const auto& t){return t.FromState==removed||t.ToState==removed;});
                    for(auto& t:draft.Transitions){if(t.FromState>removed)--t.FromState;if(t.ToState>removed)--t.ToState;}
                    draft.States.erase(draft.States.begin()+m_ControllerState);
                    if(draft.EntryState==removed)draft.EntryState=0;
                    else if(draft.EntryState>removed)--draft.EntryState;
                    m_ControllerState=m_ControllerTransition=0;
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::BeginDisabled(draft.States.size()<2||draft.Transitions.size()>=256);
                if(ImGui::Button("Add transition"))
                {
                    AnimationControllerTransition t;
                    t.FromState=0;t.ToState=1;t.MinimumNormalizedTime=1;
                    draft.Transitions.push_back(std::move(t));
                    m_ControllerTransition=static_cast<int>(draft.Transitions.size()-1);
                }
                ImGui::EndDisabled();
                if(!draft.Transitions.empty())
                {
                    ImGui::InputInt("Transition index",&m_ControllerTransition);
                    m_ControllerTransition=std::clamp(m_ControllerTransition,0,static_cast<int>(draft.Transitions.size()-1));
                    auto& t=draft.Transitions[static_cast<std::size_t>(m_ControllerTransition)];
                    indexField("From state",t.FromState,draft.States.size());
                    indexField("To state",t.ToState,draft.States.size());
                    ImGui::InputFloat("Blend seconds",&t.BlendSeconds);
                    ImGui::InputFloat("Minimum normalized time",&t.MinimumNormalizedTime);
                    int priority=t.Priority;
                    if(ImGui::InputInt("Priority",&priority))t.Priority=priority;
                    bool conditional=!t.Conditions.empty();
                    if(ImGui::Checkbox("Require parameter",&conditional))
                    {
                        t.Conditions.clear();
                        if(conditional)t.Conditions.push_back({"active",AnimationConditionOperator::Equal,true});
                    }
                    if(!t.Conditions.empty())
                    {
                        auto& condition=t.Conditions.front();
                        EditControllerName("Parameter name",condition.Parameter);
                        bool expected=std::get<bool>(condition.Value);
                        if(ImGui::Checkbox("Required value",&expected))condition.Value=expected;
                    }
                    if(ImGui::Button("Delete transition"))
                    {
                        draft.Transitions.erase(draft.Transitions.begin()+m_ControllerTransition);
                        m_ControllerTransition=0;
                    }
                }
                if(!m_ControllerError.empty())ImGui::TextWrapped("%s",m_ControllerError.c_str());
                ImGui::BeginDisabled(!canEdit);
                if(ImGui::Button("Save state machine"))
                {
                    try
                    {
                        assets.CommitController(root,asset,draft);
                        m_ControllerDraft.reset();
                        m_ControllerError.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    catch(const std::exception& e){m_ControllerError=e.what();}
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if(ImGui::Button("Cancel state machine")){m_ControllerDraft.reset();ImGui::CloseCurrentPopup();}
                ImGui::EndPopup();
            }
            definition=assets.ResolveController(asset);
            if(!definition)return;
            bool preview=m_ControllerPreview.has_value();
            if(ImGui::Checkbox("Preview state machine",&preview))
            {
                m_ControllerPreview.reset();m_Parameters.Clear();m_ControllerError.clear();
                if(preview)
                {
                    try
                    {
                        m_ControllerPreview.emplace(*definition,source);
                        for(const auto& t:definition->Transitions)
                            for(const auto& c:t.Conditions)m_Parameters.SetBool(c.Parameter,false);
                    }
                    catch(const std::exception& e){m_ControllerError=e.what();}
                }
            }
            if(m_ControllerPreview)
            {
                ImGui::Text("State: %s | blend %.2f",
                    std::string(m_ControllerPreview->CurrentStateName()).c_str(),m_ControllerPreview->TransitionAlpha());
                if(ImGui::Button("Reset machine"))m_ControllerPreview->Reset();
                // Each declared parameter appears only once, even when
                // several transitions consume it.
                std::vector<std::string> shown;
                for(const auto& t:definition->Transitions)
                    for(const auto& c:t.Conditions)
                    {
                        if(std::find(shown.begin(),shown.end(),c.Parameter)!=shown.end())continue;
                        shown.push_back(c.Parameter);
                        bool value=std::get<bool>(*m_Parameters.Find(c.Parameter));
                        ImGui::PushID(static_cast<int>(shown.size()));
                        if(ImGui::Checkbox(c.Parameter.c_str(),&value))m_Parameters.SetBool(c.Parameter,value);
                        ImGui::PopID();
                    }
            }
            if(!m_ControllerError.empty())ImGui::TextWrapped("%s",m_ControllerError.c_str());
        }

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
