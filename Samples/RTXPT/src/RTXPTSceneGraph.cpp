/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "RTXPTSceneGraph.hpp"

#include <cmath>

#include "FileSystem.hpp"
#include "RTXPTSceneJson.hpp"

namespace Diligent
{

void RTXPTSceneGraphData::Clear()
{
    ModelAssets.clear();
    GraphNodes.clear();
    ModelInstances.clear();
    MaterialExtensions.clear();
    Lights.clear();
    Settings = {};
    Stats    = {};
    Warnings.clear();
}

std::string GetRTXPTModelNameFromPath(const std::string& ModelPath)
{
    const size_t Slash = ModelPath.find_last_of("/\\");
    const size_t Start = Slash == std::string::npos ? 0 : Slash + 1;
    const size_t Dot   = ModelPath.find_last_of('.');
    return Dot != std::string::npos && Dot > Start ? ModelPath.substr(Start, Dot - Start) : ModelPath.substr(Start);
}

float4x4 MakeRTXPTNodeTransform(const nlohmann::json& Node)
{
    float Translation[3] = {0.0f, 0.0f, 0.0f};
    float Rotation[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
    float Scale[3]       = {1.0f, 1.0f, 1.0f};

    ReadRTXPTFloatArray(Node, "translation", Translation, 3);
    if (!ReadRTXPTFloatArray(Node, "rotation", Rotation, 4))
    {
        float Euler[3] = {0.0f, 0.0f, 0.0f};
        if (ReadRTXPTFloatArray(Node, "euler", Euler, 3))
        {
            QuaternionF       Qx{std::sin(Euler[0] * 0.5f), 0.0f, 0.0f, std::cos(Euler[0] * 0.5f)};
            QuaternionF       Qy{0.0f, std::sin(Euler[1] * 0.5f), 0.0f, std::cos(Euler[1] * 0.5f)};
            QuaternionF       Qz{0.0f, 0.0f, std::sin(Euler[2] * 0.5f), std::cos(Euler[2] * 0.5f)};
            const QuaternionF Q = Qx * Qy * Qz;
            Rotation[0]        = Q.q.x;
            Rotation[1]        = Q.q.y;
            Rotation[2]        = Q.q.z;
            Rotation[3]        = Q.q.w;
        }
    }

    const auto ScaleIt = Node.find("scaling");
    if (ScaleIt != Node.end())
    {
        if (ScaleIt->is_number())
        {
            Scale[0] = Scale[1] = Scale[2] = ScaleIt->get<float>();
        }
        else
        {
            ReadRTXPTFloatArray(Node, "scaling", Scale, 3);
        }
    }

    const QuaternionF Q{Rotation[0], Rotation[1], Rotation[2], Rotation[3]};
    return float4x4::Scale(Scale[0], Scale[1], Scale[2]) *
        Q.ToMatrix() *
        float4x4::Translation(Translation[0], Translation[1], Translation[2]);
}

std::vector<std::string> GetRTXPTMaterialCandidates(const std::string& AssetsRoot,
                                                    const std::string& SceneName,
                                                    const std::string& ModelName,
                                                    const std::string& MaterialName)
{
    std::string       SceneStem = SceneName;
    const std::string Suffix    = ".json";
    if (SceneStem.size() > Suffix.size() &&
        SceneStem.compare(SceneStem.size() - Suffix.size(), Suffix.size(), Suffix) == 0)
    {
        SceneStem.resize(SceneStem.size() - Suffix.size());
    }

    const std::string MaterialsRoot = AssetsRoot + "/Materials";
    return {
        MaterialsRoot + "/" + SceneStem + "/" + ModelName + "." + MaterialName + ".material.json",
        MaterialsRoot + "/" + SceneStem + "/" + MaterialName + ".material.json",
        MaterialsRoot + "/" + ModelName + "." + MaterialName + ".material.json",
        MaterialsRoot + "/" + MaterialName + ".material.json",
    };
}

} // namespace Diligent
