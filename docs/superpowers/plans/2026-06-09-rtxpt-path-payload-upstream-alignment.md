# RTXPT Path Payload Upstream Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the upstream 80-byte `PathState`/`PathPayload` contract after enabling O3 for every RTXPT shader.

**Architecture:** Treat `PathState`, `PathPayload`, stable-plane exploration storage, and the C++ RT pipeline payload limit as one shared contract. Preserve Diligent-specific rendering behavior outside that contract.

**Tech Stack:** HLSL, DXC Shader Model 6.5, Diligent Engine C++, CMake

---

### Task 1: Establish the contract test

**Files:**
- Inspect: `Samples/RTXPT/shaders/PathTracer/PathState.hlsli`
- Inspect: `Samples/RTXPT/shaders/PathTracer/PathPayload.hlsli`
- Inspect: `Samples/RTXPT/src/RTXPTRayTracingPass.cpp`

- [ ] Run a PowerShell source-contract check that requires a fixed five-lane
  payload, `flagsAndVertexIndex`, no fp32 reference fields, no reference depth
  helpers, and an 80-byte PSO payload limit.
- [ ] Confirm it fails against the current workaround implementation.

### Task 2: Restore the shader contract

**Files:**
- Modify: `Samples/RTXPT/shaders/PathTracer/PathState.hlsli`
- Modify: `Samples/RTXPT/shaders/PathTracer/PathPayload.hlsli`
- Modify: `Samples/RTXPT/shaders/PathTracer/PathTracer.hlsli`
- Modify: `Samples/RTXPT/shaders/PathTracer/PathTracerClosestHit.rchit`
- Modify: `Samples/RTXPT/shaders/PathTracer/PathTracerSample.rgen`
- Modify: `Samples/RTXPT/shaders/PathTracer/PathTracerStablePlanes.hlsli`
- Modify: `Samples/RTXPT/shaders/PathTracer/StablePlanes.hlsli`
- Modify: `Samples/RTXPT/shaders/PathTracer/PathTracerShared.h`

- [ ] Restore upstream packed accessors and the combined flags/index word.
- [ ] Restore the fixed `uint4 packed[5]` payload and direct exploration array
  access.
- [ ] Remove reference primary-depth field reuse and leave guide output to the
  bridge.
- [ ] Re-run the source-contract check and confirm it passes.

### Task 3: Synchronize the pipeline and documentation

**Files:**
- Modify: `Samples/RTXPT/src/RTXPTRayTracingPass.cpp`
- Modify: `Samples/RTXPT/RTXPT_FORK_MAPPING.md`

- [ ] Set `MaxPayloadSize` to 80 bytes.
- [ ] Remove documentation describing the retired workaround and record parity
  with the upstream payload layout.

### Task 4: Verify

- [ ] Build the RTXPT target in the existing x64 Debug build.
- [ ] Run formatting validation on modified files.
- [ ] Search for retired workaround identifiers and confirm none remain.
- [ ] Review the final diff against the approved design.

