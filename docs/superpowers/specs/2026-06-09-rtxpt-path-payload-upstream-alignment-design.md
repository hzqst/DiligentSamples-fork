# RTXPT Path Payload Upstream Alignment Design

## Goal

Remove the DXC O0 workarounds from the Diligent RTXPT port now that every RTXPT
shader is compiled with optimization level 3.

## Design

- Restore the upstream `PathState` representation with one
  `flagsAndVertexIndex` word.
- Use the upstream fp16-packed throughput, radiance, BSDF PDF, firefly, MIS, and
  Russian-roulette fields in every path-tracer mode.
- Fix `PathPayload` at five `uint4` lanes (80 bytes) for reference, build, and
  fill variants.
- Pass stable-plane exploration payloads directly through the five-lane
  `packed` array.
- Remove reference-only reuse of stable-plane fields for primary depth. Guide
  output remains owned by the Diligent bridge export functions.
- Set the ray-tracing pipeline payload limit to 80 bytes.
- Preserve Diligent resource bindings, shader entry points, bridge adapters, and
  estimator behavior that are unrelated to the O0 workaround.

## Verification

- Static contract checks must reject mode-specific payload sizes, split
  flag/index state, fp32 reference payload fields, reference depth helpers, and
  a payload limit other than 80 bytes.
- Build the RTXPT target so DXC compiles all reference/build/fill shaders with
  optimization level 3.
- Run formatting validation for all modified C++ and shader files.

