/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "RTXPTSceneJson.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

#include "DebugUtilities.hpp"

namespace Diligent
{
namespace
{

std::string ReadWholeTextFile(const std::string& FilePath, std::string& Error)
{
    std::ifstream File{FilePath};
    if (!File)
    {
        Error = "Unable to open JSON file: " + FilePath;
        return {};
    }

    std::ostringstream Strm;
    Strm << File.rdbuf();
    return Strm.str();
}

std::string RemoveTrailingJsonCommas(const std::string& Source)
{
    std::string Output;
    Output.reserve(Source.size());

    bool InString = false;
    bool Escaped  = false;

    for (size_t i = 0; i < Source.size(); ++i)
    {
        const char Ch = Source[i];
        if (InString)
        {
            Output.push_back(Ch);
            if (Escaped)
                Escaped = false;
            else if (Ch == '\\')
                Escaped = true;
            else if (Ch == '"')
                InString = false;
            continue;
        }

        if (Ch == '"')
        {
            InString = true;
            Output.push_back(Ch);
            continue;
        }

        if (Ch == ',')
        {
            size_t Next = i + 1;
            while (Next < Source.size() && std::isspace(static_cast<unsigned char>(Source[Next])) != 0)
                ++Next;
            if (Next < Source.size() && (Source[Next] == ']' || Source[Next] == '}'))
                continue;
        }

        Output.push_back(Ch);
    }

    return Output;
}

} // namespace

bool LoadRTXPTRelaxedJsonFile(const std::string& FilePath, RTXPTJsonLoadResult& Result)
{
    Result = {};

    std::string       Error;
    const std::string Text = ReadWholeTextFile(FilePath, Error);
    if (!Error.empty())
    {
        Result.Error = Error;
        LOG_ERROR_MESSAGE(Result.Error);
        return false;
    }

    Result.Json            = nlohmann::json::parse(Text, nullptr, false, true);
    Result.CommentsIgnored = true;
    if (!Result.Json.is_discarded())
        return true;

    Result.StrictParseFailed     = true;
    const std::string RelaxedText = RemoveTrailingJsonCommas(Text);
    Result.Json                  = nlohmann::json::parse(RelaxedText, nullptr, false, true);
    Result.RelaxedParseUsed      = true;
    Result.CommentsIgnored       = true;
    if (!Result.Json.is_discarded())
        return true;

    Result.Error = "Invalid JSON file after relaxed parsing: " + FilePath;
    LOG_ERROR_MESSAGE(Result.Error);
    return false;
}

bool ReadRTXPTFloatArray(const nlohmann::json& Object, const char* Key, float* Values, size_t Count)
{
    const auto It = Object.find(Key);
    if (It == Object.end() || !It->is_array() || It->size() < Count)
        return false;

    for (size_t Idx = 0; Idx < Count; ++Idx)
    {
        if (!(*It)[Idx].is_number())
            return false;
        Values[Idx] = (*It)[Idx].get<float>();
    }
    return true;
}

float ReadRTXPTOptionalFloat(const nlohmann::json& Object, const char* Key, float DefaultValue)
{
    const auto It = Object.find(Key);
    return It != Object.end() && It->is_number() ? It->get<float>() : DefaultValue;
}

std::string ReadRTXPTOptionalString(const nlohmann::json& Object, const char* Key, const char* DefaultValue)
{
    const auto It = Object.find(Key);
    return It != Object.end() && It->is_string() ? It->get<std::string>() : std::string{DefaultValue};
}

} // namespace Diligent
