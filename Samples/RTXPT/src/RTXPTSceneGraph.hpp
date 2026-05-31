/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "GLTFLoader.hpp"
#include "json.hpp"

namespace Diligent
{

using RTXPTSceneId                         = Uint32;
constexpr RTXPTSceneId InvalidRTXPTSceneId = ~Uint32{0};

struct RTXPTModelAsset
{
    std::string                  RelativePath;
    std::string                  ResolvedPath;
    std::string                  ModelName;
    std::unique_ptr<GLTF::Model> Model;
    Uint32                       SceneIndex = 0;
    Uint32                       GlobalVertexBase = 0;
    Uint32                       GlobalIndexBase  = 0;
    Uint32                       VertexCount      = 0;
    Uint32                       IndexCount       = 0;
    GLTF::ModelTransforms        StaticTransforms;
    std::vector<std::string>     MaterialNames;
    std::vector<Uint32>          MaterialRemap;
    std::vector<Uint32>          TextureRemap;
};

struct RTXPTAnimationState
{
    bool  Enabled        = true;
    Int32 AnimationIndex = -1;
    float Time           = 0.0f;
    float PlaySpeed      = 1.0f;
    float TimeOffset     = 0.0f;
};

struct RTXPTGraphNode
{
    std::string               Name;
    std::string               Type;
    RTXPTSceneId              ParentId     = InvalidRTXPTSceneId;
    RTXPTSceneId              ModelAssetId = InvalidRTXPTSceneId;
    std::vector<RTXPTSceneId> Children;
    float4x4                  LocalTransform  = float4x4::Identity();
    float4x4                  GlobalTransform = float4x4::Identity();
    nlohmann::json            RawMetadata;
};

struct RTXPTModelInstance
{
    RTXPTSceneId          GraphNodeId  = InvalidRTXPTSceneId;
    RTXPTSceneId          ModelAssetId = InvalidRTXPTSceneId;
    std::string           Name;
    float4x4              GlobalTransform = float4x4::Identity();
    RTXPTAnimationState   Animation;
    GLTF::ModelTransforms Transforms;
};

struct RTXPTMaterialExtension
{
    std::string   FilePath;
    std::string   ModelName;
    std::string   MaterialName;
    bool          Loaded = false;
    nlohmann::json RawJson;

    float4 BaseColorFactor                              = float4{1, 1, 1, 1};
    float3 EmissiveFactor                               = float3{0, 0, 0};
    float  EmissiveIntensity                            = 1.0f;
    float  MetallicFactor                               = 1.0f;
    float  RoughnessFactor                              = 1.0f;
    float  AlphaCutoff                                  = 0.5f;
    bool   EnableAlphaTesting                           = false;
    bool   EnableBaseTexture                            = true;
    bool   EnableEmissiveTexture                        = true;
    bool   EnableNormalTexture                          = true;
    bool   EnableOcclusionRoughnessMetallicTexture      = true;
    bool   EnableTransmission                           = false;
    float  TransmissionFactor                           = 0.0f;
    float  IoR                                          = 1.5f;
    bool   ThinSurface                                  = false;
    bool   SkipRender                                   = false;
};

struct RTXPTSceneLightMetadata
{
    std::string   Name;
    std::string   Type;
    float4x4      GlobalTransform = float4x4::Identity();
    nlohmann::json RawJson;
};

struct RTXPTSceneSettings
{
    bool           HasSampleSettings = false;
    bool           HasGameSettings   = false;
    nlohmann::json SampleSettingsJson;
    nlohmann::json GameSettingsJson;
};

struct RTXPTSceneAdapterStats
{
    Uint32 ModelAssetCount        = 0;
    Uint32 GraphNodeCount         = 0;
    Uint32 ModelInstanceCount     = 0;
    Uint32 MaterialCount          = 0;
    Uint32 MaterialExtensionCount = 0;
    Uint32 MaterialFallbackCount  = 0;
    Uint32 DirectionalLightCount  = 0;
    Uint32 PointLightCount        = 0;
    Uint32 SpotLightCount         = 0;
    Uint32 EnvironmentLightCount  = 0;
    Uint32 UnknownTypedNodeCount  = 0;
    Uint32 SkinnedInstanceCount   = 0;
    Uint32 AdapterWarningCount    = 0;
};

struct RTXPTSceneGraphData
{
    std::vector<RTXPTModelAsset>         ModelAssets;
    std::vector<RTXPTGraphNode>          GraphNodes;
    std::vector<RTXPTModelInstance>      ModelInstances;
    std::vector<RTXPTMaterialExtension>  MaterialExtensions;
    std::vector<RTXPTSceneLightMetadata> Lights;
    RTXPTSceneSettings                   Settings;
    RTXPTSceneAdapterStats               Stats;
    std::vector<std::string>             Warnings;

    void Clear();
};

std::string GetRTXPTModelNameFromPath(const std::string& ModelPath);
float4x4    MakeRTXPTNodeTransform(const nlohmann::json& Node);
std::vector<std::string> GetRTXPTMaterialCandidates(const std::string& AssetsRoot,
                                                    const std::string& SceneName,
                                                    const std::string& ModelName,
                                                    const std::string& MaterialName);
RTXPTMaterialExtension ParseRTXPTMaterialExtension(const std::string& FilePath,
                                                   const std::string& ModelName,
                                                   const std::string& MaterialName,
                                                   const nlohmann::json& Json);

} // namespace Diligent
