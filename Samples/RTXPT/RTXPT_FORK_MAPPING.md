# RTXPT Fork Mapping

This document records the RTXPT-fork <-> DiligentEngine-port correspondence for
the RTXPT Phase R0.5 reference path tracer migration. Its purpose is to make
future upstream RTXPT re-ports close to a mechanical diff/merge while preserving
the current DiligentSamples rendering behavior.

Reference source spec: `docs/superpowers/specs/2026-05-30-rtxpt-reference-pathtracer-completion-design.md`
(relative to the superproject root, not the `DiligentSamples` submodule root), goal G0.5.

This is a mapping document only. Later migration tasks should apply exact
whole-word, case-sensitive renames and file relocations without changing shader
math, CPU/GPU struct byte layouts, or sample-framework glue behavior.

## Naming Rule

"Full alignment" does not mean copying RTXPT-fork's implementations or byte
layouts. Our reference path tracer is a flattened raygen-loop subset, while
several RTXPT-fork structs are realtime-track-shaped, such as packed
`PathState`/`PathPayload` and `PathTracerConstants` with ReSTIR, stable-plane,
and DLSS fields, with no field-level analog to ours.

Full alignment means:

1. Use the exact RTXPT-fork name when our symbol computes the same quantity with
   a compatible signature. Examples: our GGX NDF maps to `evalNdfGGX`; our
   cosine-hemisphere PDF helper maps to the closest matching upstream-style
   helper; `Hash32`/`Hash32Combine` already match.
2. Use RTXPT-fork naming style and the closest RTXPT-fork file/folder when there
   is no exact analog. Style includes PascalCase types/functions,
   `m_`/`g_`/`t_`/`u_`/`s_`/`k`/`c_` prefixes, camelCase locals and
   parameters, HLSL `namespace` plus inline helpers, and traditional include
   guards. Examples: our bespoke `RTXPTPathTracerSettings`, our flat
   `RTXPTPathTracerPayload`, and our hybrid `RTXPTSurface`.
3. Never rename a symbol to an RTXPT-fork name whose semantics differ. Example:
   our `RTXPTVisibilitySmithGGX` returns `G/(4*NoV*NoL)` as combined
   visibility, while RTXPT-fork's `evalMaskingSmithGGXCorrelated` returns the
   masking term `G` alone. Renaming ours to that upstream name would be a lie.
   Use a port-style name such as `evalVisibilitySmithGGXCorrelated` and record
   the divergence.
4. Byte-identical output is a hard invariant. Apply renames as whole-word,
   case-sensitive find/replace and relocations only. Do not improve math,
   reorder operations, change literals, or touch struct byte layout. Every
   `static_assert(sizeof(...) == N)` must still hold unchanged.

Hard constraints:

- Files under `DiligentSamples/` keep Diligent's Apache/Diligent copyright
  header where one exists today, which applies to the C++ files.
- Shaders currently carry no header; keep them header-less.
- Do not add NVIDIA copyright headers anywhere.
- C++ sample-framework glue with no RTXPT-fork analog keeps Diligent
  conventions. This includes sample lifecycle, pass classes, and the Diligent
  binding model.
- Only GPU-facing mirrored structs and resource-binding variable names align
  with RTXPT-fork, because those are the CPU/GPU contract boundary that upstream
  merges cross.

## File-Move Map

| Current `assets/shaders/...` | New `assets/shaders/...` | RTXPT-fork analog |
|---|---|---|
| `RTXPTReference.rgen` | `PathTracer/PathTracerSample.rgen` | `PathTracer/PathTracerSample.hlsl` (raygen) |
| `RTXPTReference.rchit` | `PathTracer/PathTracerClosestHit.rchit` | hit handling in `PathTracer.hlsli`/`Scene/HitInfo.hlsli` |
| `RTXPTReference.rmiss` | `PathTracer/PathTracerMiss.rmiss` | miss handling in `PathTracer.hlsli` |
| `RTXPTReference.rahit` | `PathTracer/PathTracerAnyHit.rahit` | any-hit / alpha test |
| `RTXPTShaderShared.hlsli` | `PathTracer/PathTracerShared.h` | `PathTracer/PathTracerShared.h` |
| `RTXPTSceneBridge.hlsli` | `PathTracer/PathTracerBridge.hlsli` | `PathTracer/PathTracerBridge.hlsli` |
| `RTXPTBSDF.hlsli` | `PathTracer/Rendering/Materials/BxDF.hlsli` | `PathTracer/Rendering/Materials/BxDF.hlsli` |
| `RTXPTMaterialBridge.hlsli` | `PathTracer/Rendering/Materials/MaterialBridge.hlsli` | material loads in `PathTracerBridge*.hlsli` |
| `RTXPTLightSampling.hlsli` | `PathTracer/Lighting/PolymorphicLight.hlsli` | `PathTracer/Lighting/PolymorphicLight.hlsli` |
| `RTXPTEnvironment.hlsli` | `PathTracer/Lighting/EnvMap.hlsli` | `PathTracer/Lighting/EnvMap.hlsli` |
| `RTXPTRandom.hlsli` | `PathTracer/Utils/SampleGenerators.hlsli` | `PathTracer/Utils/SampleGenerators.hlsli` |
| `RTXPTCommon.fxh` | `RTXPTCommon.fxh` (stays flat - port compat wrapper) | none |
| `RTXPTDebugCompute.csh` | `RTXPTDebugCompute.csh` (stays flat) | none |
| `RTXPTBlit.vsh` / `RTXPTBlit.psh` | unchanged (stay flat) | none |

## Authoritative Rename Tables

### T-A. Macros, Include Guards, Constants

| Current | New | Notes |
|---|---|---|
| `RTXPT_ENABLE_HIT_BRIDGE` | `ENABLE_HIT_BRIDGE` (style) | HLSL-only define |
| `RTXPT_ENABLE_MATERIAL_TEXTURES` | `ENABLE_MATERIAL_TEXTURES` (style) | also a C++ `ShaderMacroHelper` string |
| `RTXPT_MATERIAL_TEXTURE_COUNT` | `MATERIAL_TEXTURE_COUNT` (style) | also a C++ `ShaderMacroHelper` string |
| include guards `RTXPT_*_HLSLI` | `__<NAME>_HLSLI__` (= RTXPT-fork form, e.g. `__BXDF_HLSLI__`) | RTXPT-fork uses `__NAME_HLSLI__` traditional guards |
| `RTXPT_PI` | `K_PI` = RTXPT-fork | `Utils/Math/MathConstants.hlsli` |
| `RTXPT_INV_PI` | `K_1_PI` = RTXPT-fork | |
| `RTXPT_VISIBILITY_RAY_TMIN` / `_TMAX` | `kVisibilityRayTMin` / `kVisibilityRayTMax` (style) | |
| `kRTXPTSubInstanceFlagIndexed` | `kSubInstanceFlagIndexed` (style) | also C++ `kRTXPTSubInstanceFlag_Indexed` -> `kSubInstanceFlag_Indexed` |
| `kRTXPTMaterialFlag*` (5) | `kMaterialFlag*` (style) | also C++ `kRTXPTMaterialFlag_*` |

### T-B. Utils Layer (`Utils/SampleGenerators.hlsli`)

| Current | New | Notes |
|---|---|---|
| `Hash32` / `Hash32Combine` / `Hash32ToFloat` | `Utils/NoiseAndSequences.hlsli` | R5 aligns reference-mode sample conversion; record if any hash constants intentionally remain port-specific |
| `struct RTXPTRandom` | `struct SampleGenerator` (style) | RTXPT-fork uses `SampleGeneratorType`; we keep one concrete struct |
| `RTXPTRandom_Init` | `SampleGenerator_make` (style) | |
| `NextFloat` | `sampleNext1D` = RTXPT-fork | param `inout SampleGenerator sg` |
| `NextFloat2` | `sampleNext2D` = RTXPT-fork | |
| `BuildOrthonormalBasis` | `BranchlessONB` = RTXPT-fork | params `normal, out tangent, out bitangent` |
| `SampleCosineHemisphere` | `sampleCosineHemisphere` (style) | RTXPT-fork's local-frame helper differs; keep ours and record divergence |
| (R1/G3 new) | `SampleGenerator_makeStateless` | stateless per-(pixel,vertex,sample) seed; mirrors RTXPT `UniformSampleSequenceGenerator::make` |
| `StatelessSampleGenerators.hlsli` | `Utils/StatelessSampleGenerators.hlsli` | BSDF-only Sobol/Owen sample blocks through `SampleSequenceGenerator::Generate` |
| `UniformSampleSequenceGenerator` | `UniformSampleSequenceGenerator` | Uniform fallback for BSDF sample blocks after the diffuse-bounce LD window or when the macro is disabled |
| (R1/G3 new) | `kSampleEffect_*` | mirrors RTXPT `SampleGeneratorEffectSeed` (Base/ScatterBSDF/NextEventEstimation/NEELightSampler/RussianRoulette) |
| (R2/G4 new) | `kSampleEffect_NEEEmissive` | fills the previously unused effect slot 4 for emissive-triangle NEE |

### T-C. Materials Layer (`Rendering/Materials/BxDF.hlsli`)

| Current | New | Notes |
|---|---|---|
| `struct RTXPTSurface` | `struct StandardBSDFData` (style) | carries shading normal `N`; RTXPT-fork splits `ShadingData`/`StandardBSDFData` |
| field `.N` | `.N` = RTXPT-fork | |
| field `.DiffuseAlbedo` | `.diffuse` = RTXPT-fork style | plain public field, no fp16 packing |
| field `.F0` | `.specular` = RTXPT-fork style | |
| field `.Alpha` | `.alpha` (style) | GGX alpha (= roughness^2) |
| `RTXPTMakeSurface` | `MakeStandardBSDFData` (style) | |
| `RTXPTFresnelSchlick(F0, VoH)` | `evalFresnelSchlick(f0, f90, cosTheta)` = RTXPT-fork | call with `f90=float3(1,1,1)` |
| `RTXPTDistributionGGX(NoH, Alpha)` | `evalNdfGGX(alpha, cosTheta)` = RTXPT-fork | param order swaps |
| `RTXPTVisibilitySmithGGX(NoV, NoL, Alpha)` | `evalVisibilitySmithGGXCorrelated(alpha, cosThetaO, cosThetaI)` (divergent) | returns `G/(4*NoV*NoL)`, not bare masking `G` |
| `kMinGGXAlpha` | `kMinGGXAlpha` | Near-mirror specular collapses to delta reflection |
| `sampleGGX_BVNDF` / `evalPdfGGX_BVNDF` | `Microfacet.hlsli` | Bounded-VNDF GGX reflection sampling and matching pdf |
| `evalDiffuseFrostbiteWeight` | `DiffuseReflectionFrostbite::evalWeight` | Frostbite/Disney energy-conserving diffuse |
| `MultiScatterSpecularApprox` | `MultiScatterSpecularApprox` | Turquin multi-scatter specular compensation |
| `RTXPTLuminance` | `luminance` = RTXPT-fork | |
| `RTXPTSpecularProbability` | `getSpecularProbability` (style) | |
| `RTXPTEvalBSDF(S, Wo, Wi, SpecProb, out FTimesNoL, out Pdf)` | `EvalBSDF(bsdfData, wo, wi, specProb, out f, out pdf)` (style) | combined helper |
| `RTXPTSampleBSDF(S, Wo, sample, ..., out Wi, out Weight, out Pdf)` | `SampleBSDF(bsdfData, wo, sample, out wi, out weight, out pdf, out lobe, out lobeP)` (style) | R5 consumes pregenerated BSDF samples and returns sampled lobe plus sampled-lobe probability |

### T-D. Lighting Layer (`Lighting/PolymorphicLight.hlsli`, `Lighting/EnvMap.hlsli`)

| Current | New | Notes |
|---|---|---|
| `struct RTXPTLightSample` | `struct LightSample` = RTXPT-fork | |
| fields `.Wi/.Distance/.Radiance/.Valid` | `.dir/.distance/.radiance/.valid` (style) | |
| `RTXPTInvalidLightSample` | `LightSample_make_empty` (style) | |
| `RTXPTNormalizeDirection` | `tryNormalize` (style) | |
| `RTXPTEvalAnalyticLight(Light, SurfacePos)` | `SampleAnalyticLight(light, random, surfacePos)` (style) | |
| `RTXPTEvalSky(RayDir)` | `EnvMapSampler::Eval(worldDir, lod)` | R4 routes miss and env NEE through baked env-map resources; procedural sky is a source/fallback path |
| `TriangleLight` (struct) | `EmissiveTriangle` (R2/G4 backing layout) | base+edge1+edge2+radiance; normal/area recomputed |
| `PathTracer/Lighting/LightSampler.hlsli` `NEEWeightedReservoirSampler` / `GenerateLightSample` | `PathTracer/Lighting/LightSampler.hlsli` `NEEWeightedReservoirSampler` / `GenerateDirectLightCandidate` / `SampleDirectLightNEE` | R3/G5 ports the RIS/WRS math as part of the full `LightsBaker` parity target |
| `LightSampler::SampleGlobal` proxy table | `RTXPTLightsBaker` `t_LightProxyCounters` + `t_LightSamplingProxies` | global proxy data now feeds the baker-owned light/feedback/proxy pipeline |
| `LightSampler::SampleLocal` local proxy table | `RTXPTLightsBaker` `t_LocalSamplingBuffer` | populated by baker-owned local sampling passes |
| `TriangleLight::CalcSample` | emissive bucket branch inside `GenerateDirectLightCandidate` | R2 stores `EmissiveTriangle`; R3 samples it through the RIS/WRS proxy table, still with uniform per-triangle selection inside the emissive bucket |
| `SampleTriangleUniform` / `pdfAtoW` / `MAX_SOLID_ANGLE_PDF` | `SampleTriangleUniform` / `pdfAtoW` / `kMaxSolidAnglePdf` (style) | |
| `LightsBaker` emissive triangle list | `RTXPTLights::UploadEmissiveTriangles` + `RTXPTEmissiveTrianglePass` (GPU build from current geometry) | full `LightsBaker` parity target; keep the Diligent-native scene plumbing |

### T-E. PathTracer Core (`PathTracer/PathTracer.hlsli`, `PathTracerHelpers.hlsli`, Raygen)

| Current | New | Notes |
|---|---|---|
| `RTXPTPowerHeuristic(PdfA, PdfB)` | `PowerHeuristic(nf, fPdf, ng, gPdf)` = RTXPT-fork | call with `(1, pdfA, 1, pdfB)` |
| (R1/G1 new) | `ComputeNewScatterFireflyFilterK` / `FireflyFilter` / `FireflyFilterShort` / `Average` = RTXPT-fork | runtime-gated by `fireflyFilterThreshold==0` instead of RTXPT's compile-time `RTXPT_FIREFLY_FILTER`; uses `acos`/`sqrt` not `FastACos`/`FastSqrt` |
| `RTXPTMakeDefaultPayload(HitFlag)` | `PathTracer::MakeEmptyPayload(hitFlag)` (style) | |
| `RTXPTTraceVisibility(Origin, Dir, TMax)` | `PathTracer::TraceVisibilityRay(origin, dir, tMax)` (style) | |
| `RTXPTSampleAnalyticNEE(...)` | `PathTracer::SampleAnalyticNEE(...)` (style) | |
| `RTXPTSampleEnvNEE(...)` | `PathTracer::SampleEnvironmentNEE(...)` (style) | |
| `RTXPTBSDFSampledEnvMISWeight(...)` | `PathTracer::ComputeBSDFEnvMISWeight(...)` (style) | |
| `ComputeBSDFMISForEmissiveTriangle` | raygen emissive BSDF-hit MIS (`payload.emissiveLightPdf` + power heuristic) | |
| `ToneMapACES` | `ToneMapACES` (unchanged) | port-specific |

Raygen locals become camelCase:

| Current | New |
|---|---|
| `WorldPos4` | `worldPos4` |
| `Origin` | `origin` |
| `RayOrigin` | `rayOrigin` |
| `RayDir` | `rayDir` |
| `Throughput` | `throughput` |
| `PathRadiance` | `pathRadiance` |
| `PrevBsdfPdf` | `prevBsdfPdf` |
| `PrevNormal` | `prevNormal` |
| `PrevDidEnvNEE` | `prevDidEnvNEE` |
| `MaxBounces` | `maxBounces` |
| `MaxNEEBounces` | `maxNEEBounces` |
| `EnableNEE` | `enableNEE` |
| `EnableEnvNEE` | `enableEnvNEE` |
| `Bounce` | `bounce` |
| `Pixel` | `pixel` |
| `Dimensions` | `dimensions` |
| `SampleIndex` | `sampleIndex` |
| (R1/G1 new) | `fireflyFilterK` |
| (R1/G1 new) | `ffThreshold` |
| (R1/G3 new) | `vertexIndex` |
| (R1/G3 new) | `sgCamera` / `sgNEELight` / `sgEnvNEE` / `sgScatter` / `sgRR` |
| `Jitter` | `jitter` |
| `UV` | `uv` |
| `NDC` | `ndc` |
| `NextDir` | `nextDir` |
| `Weight` | `weight` |
| `Pdf` | `pdf` |
| `Wo` | `wo` |
| `Surface` | `bsdfData` |
| `Bias` | `bias` |
| `VisibilityOrigin` | `PathTracer::MakeVisibilityOrigin(...)` |
| `UseNEE` | `useNEE` |
| `Survive` | `survive` |
| `Accumulated` | `accumulated` |
| `Reset` | `reset` |
| `Frame` | `frame` |
| `InvN` | `invN` |
| `Previous` | `previous` |
| `MISWeight` | `misWeight` |
| `EnvRadiance` | `envRadiance` |
| (R1/G1 new) | `environmentEmission` |
| (R1/G1 new) | `surfaceEmission` |
| (R1/G1 new) | `lobeP` |

### T-F. Bridge Namespace (`PathTracerBridge.hlsli`, `MaterialBridge.hlsli`)

| Current `Bridge::` | New `Bridge::` | Notes |
|---|---|---|
| `GetLightCount` | `getLightCount` (style) | |
| `GetLight` | `getLight` (style) | |
| `GetSubInstanceIndex` | `getSubInstanceIndex` (style) | |
| `GetSubInstanceData` | `getSubInstanceData` (style) | |
| `HasSubInstanceTable` | `hasSubInstanceTable` (style) | |
| `GetTriangleIndices` | `getTriangleIndices` (style) | |
| `GetTriangleVertices` | `getTriangleVertices` (style) | |
| `InterpolateNormal` / `InterpolateTexCoord` | `interpolateNormal` / `interpolateTexCoord` (style) | |
| `ComputeGeometricNormal` | `computeGeometricNormal` (style) | |
| `ComputeWorldHitPosition` | `computeWorldHitPosition` (style) | |
| `ComputeWorldTangent` | `computeWorldTangent` (style) | |
| `HasMaterialTable` / `GetMaterialCount` / `GetMaterial` | `hasMaterialTable` / `getMaterialCount` / `getMaterial` (style) | |
| `SampleMaterialTexture` | `sampleMaterialTexture` (style) | |
| `GetBaseColor` / `GetEmission` / `GetMetallicRoughness` / `GetTangentNormal` | `getBaseColor` / `getEmission` / `getMetallicRoughness` / `getTangentNormal` (style) | |
| `AlphaTestPasses` | `alphaTestPasses` (style) | |

### T-G. Shared CPU/GPU Structs - Type Names

| Current type | New type | RTXPT-fork |
|---|---|---|
| `RTXPTSubInstanceData` | `SubInstanceData` = RTXPT-fork | `Shaders/SubInstanceData.h` |
| `RTXPTPathTracerSettings` | `PathTracerConstants` = RTXPT-fork (name) | `PathTracerShared.h` (fields differ - divergent) |
| `RTXPTFrameConstants` | `SampleConstants` (style) | RTXPT-fork `SampleConstantBuffer.h` |
| `RTXPTPathTracerPayload` | `PathPayload` = RTXPT-fork (name) | `PathPayload.hlsli` (ours is unpacked - divergent) |
| `RTXPTPrimaryPayload` | `PrimaryPayload` (style) | compatibility-only |
| `RTXPTMaterialData` | `MaterialPTData` = RTXPT-fork (name) | `Materials/MaterialPT.h` (fields differ - divergent) |
| `RTXPTLightData` | `PolymorphicLightInfo` = RTXPT-fork (name) | `Lighting/PolymorphicLight.h` (ours is unpacked - divergent) |
| `RTXPTVertex` | `GeometryVertexData` (style) | RTXPT-fork uses separate attribute buffers - divergent |

### T-H. Shared Struct Field Names

| Struct | Current | New |
|---|---|---|
| `SubInstanceData` | `MaterialID` | `MaterialID` |
| `SubInstanceData` | `Flags` | `Flags` |
| `SubInstanceData` | `FirstIndex` | `IndexOffset` |
| `SubInstanceData` | `IndexCount` | `IndexCount` |
| `SubInstanceData` | `FirstVertex` | `VertexOffset` |
| `SubInstanceData` | `VertexCount` | `VertexCount` |
| `SubInstanceData` | `Padding0` / `Padding1` | `_padding0` / `_padding1` |
| `PathTracerConstants` | `MaxBounces` | `bounceCount` |
| `PathTracerConstants` | `AccumulationFrame` | `sampleIndex` |
| `PathTracerConstants` | `ResetAccumulation` | `resetAccumulation` |
| `PathTracerConstants` | `MinBounces` | `minBounceCount` |
| `PathTracerConstants` | `EnableNEE` | `NEEEnabled` |
| `PathTracerConstants` | `EnableEnvNEE` | `environmentNEEEnabled` |
| `PathTracerConstants` | `EnvIntensity` | `environmentIntensity` |
| `PathTracerConstants` | `LightIntensityScale` | `lightIntensityScale` |
| `PathTracerConstants` | `MaxNEEBounces` | `maxNEEBounceCount` |
| `PathTracerConstants` | `AnalyticLightCount` | `analyticLightCount` |
| `PathTracerConstants` | `Padding1` / `Padding2` | `_padding0` / `exposureScale` |
| `SampleConstants` | `ViewProj` | `viewProj` |
| `SampleConstants` | `ViewProjInv` | `viewProjInv` |
| `SampleConstants` | `CameraPosition_Time` | `cameraPositionAndTime` |
| `SampleConstants` | `ViewportSize_FrameIdx` | `viewportSizeAndFrameIndex` |
| `SampleConstants` | `PathTracer` | `ptConsts` |
| `PathPayload` | `WorldPos` | `worldPos` |
| `PathPayload` | `HitDistance` | `hitDistance` |
| `PathPayload` | `WorldNormal` | `worldNormal` |
| `PathPayload` | `HitFlag` | `hitFlag` |
| `PathPayload` | `BaseColor` | `baseColor` |
| `PathPayload` | `Metallic` | `metallic` |
| `PathPayload` | `Emission` | `emission` |
| `PathPayload` | `Roughness` | `roughness` |
| `MaterialPTData` | `BaseColorFactor` | `baseColorFactor` |
| `MaterialPTData` | `EmissiveFactor` | `emissiveFactor` |
| `MaterialPTData` | `AlphaCutoff` | `alphaCutoff` |
| `MaterialPTData` | `Flags` | `flags` |
| `MaterialPTData` | `BaseColorTextureIndex` | `baseColorTextureIndex` |
| `MaterialPTData` | `EmissiveTextureIndex` | `emissiveTextureIndex` |
| `MaterialPTData` | `MetallicFactor` | `metallicFactor` |
| `MaterialPTData` | `RoughnessFactor` | `roughnessFactor` |
| `MaterialPTData` | `BaseColorTextureSlice` | `baseColorTextureSlice` |
| `MaterialPTData` | `EmissiveTextureSlice` | `emissiveTextureSlice` |
| `MaterialPTData` | `MetallicRoughnessTextureIndex` | `metallicRoughnessTextureIndex` |
| `MaterialPTData` | `MetallicRoughnessTextureSlice` | `metallicRoughnessTextureSlice` |
| `MaterialPTData` | `NormalTextureIndex` | `normalTextureIndex` |
| `MaterialPTData` | `NormalTextureSlice` | `normalTextureSlice` |
| `MaterialPTData` | `NormalScale` | `normalScale` |
| `MaterialPTData` | `Padding0` / `Padding1` / `Padding2` / `Padding3` | `_padding0` / `_padding1` / `_padding2` / `_padding3` |
| `PolymorphicLightInfo` | `ColorType` | `colorType` |
| `PolymorphicLightInfo` | `PositionRadius` | `positionRadius` |
| `PolymorphicLightInfo` | `DirectionRange` | `directionRange` |
| `PolymorphicLightInfo` | `Shaping` | `shaping` |
| `GeometryVertexData` | `Position` | `position` |
| `GeometryVertexData` | `Normal` | `normal` |
| `GeometryVertexData` | `TexCoord0` | `texCoord0` |

### T-I. Resource-Binding Variable Names

| Current HLSL global | New | C++ touch points |
|---|---|---|
| `g_FrameConstants` | `g_Const` | `RTXPTRayTracingPass.cpp`, `RTXPTCommon.fxh`, all shaders |
| `g_TLAS` | `t_SceneBVH` | `RTXPTRayTracingPass.cpp` |
| `g_OutputColor` (rgen UAV) | `u_Output` | `RTXPTRayTracingPass.cpp` |
| `g_AccumColor` | `u_AccumulationBuffer` | `RTXPTRayTracingPass.cpp` |
| `g_Lights` | `t_Lights` | `RTXPTRayTracingPass.cpp` |
| `g_EmissiveTriangles` | `t_EmissiveTriangles` | `RTXPTRayTracingPass.cpp`, `RTXPTLights.cpp` |
| `g_SubInstanceData` | `t_SubInstanceData` | `RTXPTRayTracingPass.cpp` |
| `g_VertexBuffer` | `t_VertexBuffer` | `RTXPTRayTracingPass.cpp` |
| `g_IndexBuffer` | `t_IndexBuffer` | `RTXPTRayTracingPass.cpp` |
| `g_Materials` | `t_PTMaterialData` | `RTXPTRayTracingPass.cpp` |
| `g_MaterialTextures` | `t_BindlessTextures` | `RTXPTRayTracingPass.cpp` |
| `g_MaterialSampler` | `s_MaterialSampler` | `RTXPTRayTracingPass.cpp` |
| `g_InputColor` (debug-compute) | `t_InputColor` | optional/debug scope |
| `g_OutputColor` (debug-compute) | `u_Output` | optional/debug scope |

## Divergences

- Everything marked `(style)` in T-A..T-I is a lexical or casing alignment
  only. The exceptions below are the cases where the port must keep its own
  semantics or layout.
- Macro and guard renames drop the port prefix or adopt RTXPT-fork casing. R5
  uses `kMinGGXAlpha` as the GGX alpha floor and collapses near-mirror
  specular events to delta reflection.
- `SampleGenerator` and the camelCase sampling helpers keep the port's concrete
  RNG implementation while R5 routes hash-to-float conversion through
  `Hash32ToFloat`. `sampleCosineHemisphere` still
  returns a basis-rotated world-space direction, unlike RTXPT-fork's local-frame
  helper.
- The Diligent port uses a raygen-flattened N-bounce loop. RTXPT-fork uses a
  packed `PathState`/`PathPayload` state machine plus stable planes.
- `PathTracer::` helpers and raygen locals follow RTXPT-fork style, but they
  remain wrappers around the flattened reference-mode loop.
- The flattened loop now preserves endpoint emission at the diffuse limit.
  Rough specular events (`roughness > 0.25`) count as diffuse-like for diffuse
  bounce limiting to match R5 reference-mode behavior.
- `Bridge::` runs over Diligent structured buffers, not Donut/NVRHI. There is
  no `PathTracerBridgeDonut.hlsli` equivalent here.
- `StandardBSDFData` carries the shading normal `N`, while R5 aligns the
  reflection model with bounded-VNDF GGX, Frostbite/Disney diffuse, and Turquin
  multi-scatter specular compensation.
- `evalVisibilitySmithGGXCorrelated` returns `G/(4*NoV*NoL)`, not RTXPT-fork's
  bare masking `G`.
- Emissive-triangle area lights (R2/G4) are **two-sided** (`abs(cosTheta)` in both the
  NEE estimator and the BSDF-hit MIS), unlike RTXPT-fork's one-sided `TriangleLight`.
  This preserves the port's pre-R2 two-sided emissive look and stays unbiased.
  Textured-emissive triangles are excluded from NEE (BSDF-only) for now. R3/G5 now
  covers the full `LightsBaker`/local-feedback system; the remaining question is how
  to reconcile the Diligent scene's emissive-triangle semantics with the upstream
  one-sided baker inputs. TODO: align one-sided + double-sided baker semantics.
- Resource/global names follow the RTXPT-fork `t_/u_/s_/g_` prefix scheme, but
  the set here is the reference-mode subset only. `t_PTMaterialData` is the
  material-buffer resource/global name backed by the local `MaterialPTData`
  type.
- `MaterialPTData`, `PolymorphicLightInfo`, `GeometryVertexData`,
  `PathPayload`, `PathTracerConstants`, and `SampleConstants` reuse RTXPT-fork
  names or style as local backing layouts. `MaterialPTData` remains
  port-specific and is not field-compatible with upstream `PTMaterialData`; the
  other backing layouts also remain port-specific and unpacked.
- Shared struct fields follow T-H's upstream-style casing/alignment where
  applicable. Fields kept identical in T-H, such as `MaterialID`, `Flags`,
  `IndexCount`, and `VertexCount`, remain intentionally unchanged, and every
  `static_assert(sizeof(...) == N)` byte-layout contract stays unchanged.
- R4 routes environment lighting through `RTXPTEnvMapBaker` and
  `EnvMapSampler`: HDR/procedural input baking, processed cubemap output,
  importance/radiance maps, BRDF LUT generation, and Diligent-native RT
  resource binding. The procedural gradient remains as the `==PROCEDURAL_SKY==`
  source/fallback path instead of a shader-only runtime environment.
- `BxDF.hlsli` keeps the Fresnel/Microfacet helpers together in one file, while
  RTXPT-fork splits them into separate helper headers.
- `RTXPTCommon.fxh`, `RTXPTDebugCompute.csh`, and `RTXPTBlit.vsh` / `psh` keep
  their `RTXPT`-prefixed names because they sit outside the algorithm layer.

## Phase R4 EnvMapBaker Mapping

| RTXPT-fork | Diligent RTXPT | Notes |
|---|---|---|
| `Lighting/Distant/EnvMapBaker.*` | `src/RTXPTEnvMapBaker.*` | Diligent-native resource owner; uses DiligentFX cubemap/BRDF precompute and local compute importance-map passes |
| `Lighting/Distant/EnvMapImportanceSamplingBaker.*` | `src/RTXPTEnvMapBaker.*` + `Lighting/EnvMapImportanceBaker.hlsl` | Builds R32 importance and RGBA16F radiance mip chains for MIP descent |
| `SampleProceduralSky` | procedural source path in `RTXPTEnvMapBaker` | Current port bakes the existing procedural gradient into the same env-map path; source sentinel is `==PROCEDURAL_SKY==` |
| `PathTracer/Lighting/EnvMap.hlsli` | `PathTracer/Lighting/EnvMap.hlsli` | Runtime `EnvMapSampler`, MIP-descent sampling, and env pdf evaluation |
| global env bindings `t_EnvironmentMap`, `t_EnvironmentImportanceMap`, `t_EnvironmentRadianceMap` | RT static resources in `RTXPTRayTracingPass` | Bound for raygen and miss with stable fallback views; BRDF LUT is generated by the baker and remains available for the later composite path |

## Phase R6 Transmission and Nested Dielectrics Mapping

| Diligent port | RTXPT-fork source | R6 notes |
|---|---|---|
| `Rendering/Materials/BxDF.hlsli` transmission helpers | `Rendering/Materials/BxDF.hlsli::SpecularReflectionTransmissionMicrofacet` | Rough dielectric reflection/transmission with Fresnel lobe selection and refraction Jacobian |
| `Rendering/Materials/LobeType.hlsli` | `Rendering/Materials/LobeType.hlsli` | Reflection/transmission lobe bit layout kept compatible with RTXPT-fork |
| `Rendering/Materials/InteriorList.hlsli` | `Rendering/Materials/InteriorList.hlsli` | Two-slot priority stack; raygen-local in Diligent instead of packed into RTXPT `PathState` |
| `PathTracerNestedDielectrics.hlsli` | `PathTracerNestedDielectrics.hlsli` | False-hit rejection, outside-IoR computation, and stack updates after transmission scatter |
| `Rendering/Volumes/HomogeneousVolumeSampler.hlsli` | `Rendering/Volumes/HomogeneousVolumeSampler.hlsli` | Absorption-only Beer-Lambert transmittance; no scattering in R6 |
| `MaterialPTData` R6 fields | `Materials/MaterialPT.h::PTMaterialData` | Diligent layout differs but carries transmission, IoR, nested priority, and attenuation data |
| `PathTracerClosestHit.rchit` two-sided payload | `PathTracerBridgeDonut.hlsli::loadSurface` | Diligent keeps closest-hit payload return style instead of RTXPT-fork `SurfaceData` |
| `PathTracerSample.rgen` interior-list loop | `PathTracer.hlsli::HandleHit` | Diligent raygen loop owns path state; rejected hits do not consume bounce count |
| `PathTracerAnyHit.rahit` stochastic alpha blend | `PathTracerMaterialSpecializations.hlsl::ANYHIT_ENTRY` | Diligent extends the alpha-test any-hit path with material-flagged stochastic alpha blend; any-hit also stays texture-optional |

## Phase R7 - Shadow/AA Polish

| Diligent port | RTXPT-fork source | R7 notes |
|---|---|---|
| `PathTracerHelpers.hlsli::ComputeRayOrigin` | `PathTracer/PathTracerHelpers.hlsli::ComputeRayOrigin` | Same robust offset algorithm, expressed without RTXPT-fork's `select()` helper |
| `PathTracerHelpers.hlsli::ComputeLowGrazingAngleFalloff` | `PathTracer/PathTracerHelpers.hlsli::ComputeLowGrazingAngleFalloff` | Same direct-light shadow terminator fadeout formula |
| `PathTracer::MakeVisibilityOrigin` | `PathTracer/PathTracerNEE.hlsli::ComputeVisibilityRay` | Diligent keeps visibility tracing in raygen helpers; face-normal side is still selected by the shading normal |
| `PathPayload.vertexNormal` | `Scene/ShadingData.hlsli::vertexN` | Closest-hit payload carries the corrected pre-normal-map vertex normal because Diligent does not materialize `ShadingData` |
| `MaterialPTData.shadowNoLFadeout` | `Materials/MaterialPT.h::ShadowNoLFadeout` | Stored in existing Diligent padding at offset 136; material record stays 144 bytes |
| `PathTracerCameraData` + `ComputeRayThinlens` | `PathTracerShared.h::PathTracerCameraData` + `PathTracerHelpers.hlsli::ComputeRayThinlens` | Same camera basis and thin-lens math; Diligent stores the camera block at top-level `SampleConstants.camera` |
| `RTXPTSample` UI labels `Aperture` / `Focal Distance` | `SampleUI.cpp` camera section | Same labels, defaults, and clamp ranges; aperture 0 is the default pinhole path |

## Skinned glTF Current Geometry

The Diligent RTXPT port uses a Diligent-native current-geometry path for skinned glTF:

- static primitives read the original GLTF vertex buffer 0
- skinned glTF node instances write current-frame vertices into `RTXPTSkinnedGeometry`
- `SubInstanceData::Flags & kSubInstanceFlag_Skinned` selects the skinned vertex arena in `PathTracerBridge.hlsli`
- skinned BLAS records update from that same arena before ray dispatch

This intentionally differs from RTXPT-fork's scene framework and keeps the invariant needed by emissive-triangle R2 work: BLAS, closest-hit fetch, and future emissive-triangle extraction must all consume the same current-frame GPU geometry. Bind-pose fallback is not allowed for skinned emissive meshes.
