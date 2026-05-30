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
| `RTXPT_MIN_ROUGHNESS` | `kMinRoughness` (divergent) | RTXPT-fork's `kMinGGXAlpha=0.0064` is a different quantity; keep value `0.045` |
| `RTXPT_VISIBILITY_RAY_TMIN` / `_TMAX` | `kVisibilityRayTMin` / `kVisibilityRayTMax` (style) | |
| `kRTXPTSubInstanceFlagIndexed` | `kSubInstanceFlagIndexed` (style) | also C++ `kRTXPTSubInstanceFlag_Indexed` -> `kSubInstanceFlag_Indexed` |
| `kRTXPTMaterialFlag*` (5) | `kMaterialFlag*` (style) | also C++ `kRTXPTMaterialFlag_*` |

### T-B. Utils Layer (`Utils/SampleGenerators.hlsli`)

| Current | New | Notes |
|---|---|---|
| `Hash32` | `Hash32` = RTXPT-fork | already matches |
| `Hash32Combine` | `Hash32Combine` = RTXPT-fork | already matches |
| `ToFloat0To1` | `UintToFloat01` (style) | keep our impl |
| `struct RTXPTRandom` | `struct SampleGenerator` (style) | RTXPT-fork uses `SampleGeneratorType`; we keep one concrete struct |
| `RTXPTRandom_Init` | `SampleGenerator_make` (style) | |
| `NextFloat` | `sampleNext1D` = RTXPT-fork | param `inout SampleGenerator sg` |
| `NextFloat2` | `sampleNext2D` = RTXPT-fork | |
| `BuildOrthonormalBasis` | `BranchlessONB` = RTXPT-fork | params `normal, out tangent, out bitangent` |
| `SampleCosineHemisphere` | `sampleCosineHemisphere` (style) | RTXPT-fork's local-frame helper differs; keep ours and record divergence |
| (R1/G3 new) | `SampleGenerator_makeStateless` | stateless per-(pixel,vertex,sample) seed; mirrors RTXPT `UniformSampleSequenceGenerator::make`. Full Sobol/Owen deferred to R5 (G9) |
| (R1/G3 new) | `kSampleEffect_*` | mirrors RTXPT `SampleGeneratorEffectSeed` (Base/ScatterBSDF/NextEventEstimation/NEELightSampler/RussianRoulette) |

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
| `RTXPTLuminance` | `luminance` = RTXPT-fork | |
| `RTXPTSpecularProbability` | `getSpecularProbability` (style) | |
| `RTXPTEvalBSDF(S, Wo, Wi, SpecProb, out FTimesNoL, out Pdf)` | `EvalBSDF(bsdfData, wo, wi, specProb, out f, out pdf)` (style) | combined helper |
| `RTXPTSampleBSDF(S, Wo, ..., out Wi, out Weight, out Pdf)` | `SampleBSDF(bsdfData, wo, inout sg, out wi, out weight, out pdf, out lobeP)` (style) | R1/G1 returns sampled-lobe probability for firefly K updates |

### T-D. Lighting Layer (`Lighting/PolymorphicLight.hlsli`, `Lighting/EnvMap.hlsli`)

| Current | New | Notes |
|---|---|---|
| `struct RTXPTLightSample` | `struct LightSample` = RTXPT-fork | |
| fields `.Wi/.Distance/.Radiance/.Valid` | `.dir/.distance/.radiance/.valid` (style) | |
| `RTXPTInvalidLightSample` | `LightSample_make_empty` (style) | |
| `RTXPTNormalizeDirection` | `tryNormalize` (style) | |
| `RTXPTEvalAnalyticLight(Light, SurfacePos)` | `EvalAnalyticLight(light, surfacePos)` (style) | |
| `RTXPTEvalSky(RayDir)` | `EnvMap::Eval(worldDir)` = RTXPT-fork | procedural sky wrapped in `namespace EnvMap` |

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
| `VisibilityOrigin` | `visibilityOrigin` |
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
| `PathTracerConstants` | `Padding1` / `Padding2` | `_padding0` / `_padding1` |
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
| `PolymorphicLightInfo` | `ColorIntensity` | `colorIntensity` |
| `PolymorphicLightInfo` | `PositionRange` | `positionRange` |
| `PolymorphicLightInfo` | `DirectionType` | `directionType` |
| `PolymorphicLightInfo` | `SpotAngles` | `spotAngles` |
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
- Macro and guard renames drop the port prefix or adopt RTXPT-fork casing, but
  `kMinRoughness` remains the port's roughness floor, not RTXPT-fork's
  `kMinGGXAlpha`.
- `SampleGenerator`, `UintToFloat01`, and the camelCase sampling helpers keep
  the port's concrete RNG implementation. `sampleCosineHemisphere` still
  returns a basis-rotated world-space direction, unlike RTXPT-fork's local-frame
  helper.
- The Diligent port uses a raygen-flattened N-bounce loop. RTXPT-fork uses a
  packed `PathState`/`PathPayload` state machine plus stable planes.
- `PathTracer::` helpers and raygen locals follow RTXPT-fork style, but they
  remain wrappers around the flattened reference-mode loop.
- `Bridge::` runs over Diligent structured buffers, not Donut/NVRHI. There is
  no `PathTracerBridgeDonut.hlsli` equivalent here.
- `StandardBSDFData` carries the shading normal `N`, and the port remains a
  two-lobe Lambert+GGX subset rather than RTXPT-fork's split
  `ShadingData`/`StandardBSDFData` model.
- `evalVisibilitySmithGGXCorrelated` returns `G/(4*NoV*NoL)`, not RTXPT-fork's
  bare masking `G`.
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
- `EnvMap::Eval` returns a procedural gradient for now; HDR map sampling is
  deferred to Phase R4.
- `BxDF.hlsli` keeps the Fresnel/Microfacet helpers together in one file, while
  RTXPT-fork splits them into separate helper headers.
- `RTXPTCommon.fxh`, `RTXPTDebugCompute.csh`, and `RTXPTBlit.vsh` / `psh` keep
  their `RTXPT`-prefixed names because they sit outside the algorithm layer.
