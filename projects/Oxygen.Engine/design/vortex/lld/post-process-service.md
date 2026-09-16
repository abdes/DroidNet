# PostProcessService LLD

Status: `in_progress` — exposure contract checkpoint; implementation follows the
[ten-slice delivery plan](../plan/exposure-and-lightbench-correction.md).
Historical VTX-M03 closure remains at its original fixed/auto baseline scope.

## Ownership and public boundary

PostProcessService owns exposure settings resolution, persistent GPU state keyed
by producer-owned `ViewStateHandle`, early frame-exposure resolve, Stage-22
exposure/bloom/tonemapping, and bounded completed status. Renderer Core owns
view registration, relationship validation and transient transition routing.
SceneRenderer supplies the exact scene signal/SRV, depth/SRV and post target;
post-process passes cannot invent another input or output routing path.

ViewId is a frame-publication identity, never a temporal-history key. Native
applications and DemoShell submit the same public typed events and canonical
per-view exposure overrides. An override replaces exposure settings for that
view without mutating the scene. The native canonical authored settings type
lives in Scene/ExposureSettings.h; enums and scalar math stay in
Core/Types/PostProcess.h. This keeps Data::AssetKey out of Core's dependency
boundary while Scene, Vortex and adapters consume one settings vocabulary.
Resource references use the existing resource-descriptor/AssetKey mechanism.

No local exposure, new temporal upscaler, second meter, legacy Renderer path,
or independent exposure/precision framework belongs to this delivery.
Equations and tolerances are owned by the
[PBR specification](../../renderer-core/physically-based-rendering.md).

## Settings resolution

Resolve scene defaults, physical camera parameters for ManualCamera, and explicit
per-view override at a frame boundary. Validate all fields as one revision:
finite values, recognized enums, positive key/camera parameters/D, nonnegative
speeds/target, ordered EV range, `0<=low<high<=1`, positive histogram span,
black influence [0,1], radius nonnegative, and <=64 finite sorted curve keys.
Validate coupled resulting gain across the complete meter/curve interval and
initial/seed solutions against the operational domain. Never silently replace
zero target or valid small gains with a positive floor.

Mask and curve uploads are immutable for each settings revision. A pending mask
keeps the previous valid settings and resource; failure reports an error and
keeps that revision. Missing mask means unit weight only when no mask was
authored. Publish scalar settings and their mask/curve resources atomically.
Requested values and active revision are separately observable.

## GPU record layouts

All GPU scalar fields are 32-bit. C++ records are standard-layout, aligned to
16 bytes, with size and every field offset asserted against the HLSL contract.
64-bit counters use two uint32 words, low then high, avoiding shader-model
requirements beyond the existing SM6.6 baseline. Compare the full counter; no
truncated generation or ViewStateHandle comparison is permitted.

### FrameExposureData: 16 bytes

| Offset | Type | Field |
| --- | --- | --- |
| 0 | float | pre_exposure |
| 4 | float | one_over_pre_exposure |
| 8 | uint | global_exposure_state_slot |
| 12 | uint | flags |

Use the existing ViewFrameBindings.view_color_frame_slot (byte 12) for this
record, renamed frame_exposure_slot with its C++/HLSL consumers in the same
migration. ViewFrameBindings stays 64 bytes; other slots retain offsets.
Remove ViewColorData.exposure and GetExposure after migration. Flags: bit 0
bootstrap/recovery FP32, bit 1 borrowed prior state, bit 2 transient diagnostic
unit gain, bit 3 source-initialization fallback. Reserved bits are zero.

### ExposureStateData: 80 bytes

| Offset | Type | Field / meaning |
| --- | --- | --- |
| 0 | float | displayed_scale S (zero allowed) |
| 4 | float | target_scale (zero allowed) |
| 8 | float | latent_scale (strictly positive) |
| 12 | float | latent_target_scale (strictly positive) |
| 16 | float | raw_metered_luminance |
| 20 | float | raw_metered_ev |
| 24 | uint | flags |
| 28 | uint | fallback_reason |
| 32 | uint2 | settings_revision |
| 40 | uint2 | requested_generation |
| 48 | uint2 | applied_generation |
| 56 | uint2 | frame_sequence |
| 64 | float | fp16_candidate_pre_exposure |
| 68 | uint | fp16_eligible_streak |
| 72 | uint2 | product_layout_revision |

State flags: history valid, initialized, meter luminance valid, meter EV valid,
synthetic dark solve, range failure, displayed-zero target, borrowed continuity,
and FP16 eligible occupy bits 0..8 respectively. Remaining bits are zero.
Fallback reason enum: None=0, MissingHistory=1, InvalidMeter=2,
SourceUninitialized=3, SourceDestroyed=4, RangeFailure=5. A dark solve marks EV
valid but does not claim exact measured luminance. Last valid metered fields
remain stored on invalid input; current validity describes the current frame.

### ExposureCompletedStatus: 80 bytes

| Offset | Type | Field / meaning |
| --- | --- | --- |
| 0 | uint2 | view_state_identity |
| 8 | uint2 | frame_sequence |
| 16 | uint2 | settings_revision |
| 24 | uint2 | requested_generation |
| 32 | uint2 | applied_generation |
| 40 | uint2 | product_layout_revision |
| 48 | uint | flags (valid state, range failure, FP16 eligible) |
| 52 | uint | first_failure_product (zero means none) |
| 56 | uint | first_failure_kind |
| 60 | uint | fp16_eligible_streak |
| 64 | uint2 | candidate_state_generation |
| 72 | uint2 | reserved, zero |

Status contains identity/eligibility, not a CPU numerical-gain authority. The
candidate_state_generation references a retained GPU state record. Pin that
record until acknowledgment is consumed or discarded and all GPU readers
retire. Each in-flight frame has a distinct status allocation. Atomic first
failure wins using a compare-exchange on product ID; status completion requires
all producer writes before copying to the existing asynchronous readback ring.
Product IDs are assigned in SceneTextures' domain inventory.

A source curve key is two float32 values, eight bytes: EV at 0, compensation at 4.
Resolve its compensation, calibration/target logarithms and bounded-EV
subtraction together using compensated CPU arithmetic. Compile the resulting
piecewise-linear log-gain target at the union of authored knots, two EV clamp
edges and two histogram-window endpoints (at most 68 runtime keys). This is the
same target function evaluated at raw metered EV, with every GPU ordinate in
[-32,32]; large cancelling authored values never require a linear intermediate.
Seed/dark/initial solves use the same exact combination before float conversion.

`ExposureTargetData` is a 560-byte structured record: uint key_count at 0,
uint flags at 4 (locked=1, zero-target=2), float initial_log_gain at 8,
float dark_log_gain at 12, then 68 float2 `(raw_ev, log_gain)` entries at 16.
Unused entries are zero. Publish it through the existing per-view transient
structured publisher and frame slot retirement. The authored/packed limit stays
64 keys; these additional points represent clamp/window boundaries, not new
authored controls. This normalization avoids both intermediate exp2 overflow
and loss of small key bias during large compensation cancellation.

The histogram allocation has 256 uint bins followed by counters for finite,
weighted, exact-black, positive-below-window and rejected samples (20 bytes),
plus 12 zero padding bytes: 1056 bytes total. Counts and mass are distinct.
Reuse existing upload/descriptor allocators and fence retirement.

## Frame sequencing and GPU lifetime

```mermaid
sequenceDiagram
    participant Game
    participant Views as ViewLifecycleService
    participant Post as PostProcessService
    participant GPU
    Game->>Views: handle + settings + transition generation
    Views->>Post: validated owner/root and frame snapshot
    Post->>GPU: resolve immutable FrameExposureData from prior state
    GPU->>GPU: UAV to SRV ordering before first HDR producer
    GPU->>GPU: scene writes P*C_scene; record range/eligibility
    Post->>GPU: Stage 22 histogram and owner-only current state solve
    GPU->>GPU: current state UAV to SRV ordering
    GPU->>GPU: bloom and tonemap consume S/P
    GPU->>GPU: copy completed status after all producers
    GPU-->>Post: fence-completed asynchronous status
    Post->>Post: matching format eligibility only; retire leased generations
```

Freeze source/prior generations for the entire logical frame before executing
any view. Allocate a distinct current generation rather than overwriting a
buffer still read by another view or frame. Publish only successfully submitted
work. Failed recording/submission leaves requests pending. An owner updates at
most once per logical frame, even if multiple products use its view. All resource
and descriptor releases wait for the last consuming fence, including readbacks
and source-destruction copies. Frame allocation count follows actual frames in
flight, not a hard-coded two-buffer assumption.

## Lifecycle and precedence

Resolve settings changes, mode and event in one frame snapshot. Highest request
generation wins; resubmission of identical contents is idempotent. Conflicting
contents at the same generation are invalid. Explicit policy wins over implicit
first-use/cut policy. Remeter/Seed in Manual or disabled mode is rejected with a
diagnostic, never saved for a later mode. A borrower cannot reset its root.

For an independent ordinary view, solve in this order:

1. Authored disabled -> S=1; Manual/ManualCamera -> authored gain immediately.
2. Explicit Auto Seed -> current target/bias/curve evaluated at requested EV;
   do not clamp seed EV to meter bounds. Publish for this event frame; consume
   generation even without a valid histogram. Zero target still displays zero.
3. Locked Auto range -> fixed solve without histogram; consume pending remeter.
4. Zero-target entry -> displayed zero immediately; continue positive latent solve.
5. Positive-target restoration -> current meter, else last valid EV, else normal
   initialization. Never invert former displayed zero.
6. Pending remeter/new Auto -> first valid current measurement applies directly;
   retain pending generation during invalid metering.
7. Manual-to-Auto or destroyed-source continuity -> retain transferred gain for
   one transition frame, then adapt independently. Zero/locked rules win.
8. Ordinary initialized Auto -> exact hybrid adaptation; invalid meter retains
   gain/last valid measurements and marks current input invalid.

Missing history with invalid meter uses the source/owner Auto EV0 fallback,
clamped to its EV bounds and evaluated with its target, key, bias and curve;
never use the inactive Manual EV. Initialization remains pending. Explicit seeds
outside meter range are legal when the resulting gain is numerically supported.

A new view, camera cut, replaced world or device recovery remeters by default.
Preserve and Seed are explicit alternatives. Walking, streaming, light changes,
compatible resize and format changes preserve exposure. Stateless Auto uses
transient state, FP32 metering and no temporal adaptation on every invocation.

A temporary diagnostic unit-gain override creates transient frame output while
preserving authored mode, history and pending events. It is distinct from
authored disabled exposure. On leaving it, apply any pending cut/reset; otherwise
resume retained history. Camera/color history invalidation remains with its owner.

```mermaid
stateDiagram-v2
    [*] --> Pending: new Auto or remeter
    Pending --> Ready: valid meter or locked solve or explicit seed
    Pending --> Pending: invalid meter / failed submission
    Ready --> Ready: ordinary adaptation / pause / resize
    Ready --> Pending: cut or remeter request
    Ready --> Ready: retained FP32 (no exposure reset)
    Ready --> Retiring: destroy handle
    Pending --> Retiring: destroy handle
    Retiring --> [*]: last reader fence completed
```

## Source-owned sharing

Resolve chains to one registered root; reject cycles/unknown handles atomically.
Only the root meters/writes its state. Borrowers use pinned published prior
source state for both displayed gain and numerical P, independent of render
order, with one frame of result latency. Local borrower exposure settings do
not modify borrowed gain. If root is inactive, retain its last publication.
With no publication use the root's disabled/manual/seed/EV0 initialization gain;
never initialize from a consumer image. Bootstrap suitability remains per view.

A borrowing-view cut resets its camera/color histories and bootstrap, not root
exposure. Explicit detachment remeters by default; Preserve/Seed are opt-in and
do not revive dormant independent history.

On root destruction detach all affected chains at the next boundary. Copy last
borrowed displayed and positive latent gain into each consumer-owned state
before retiring root resources. Auto retains that gain for one frame, then
meters independently; Manual applies its setting and disabled uses one. Consumer
zero target wins. If borrowed displayed gain was zero but local target positive,
retain zero only for the continuity frame, then use the positive latent gain
while awaiting a valid meter. Missing publication uses captured root fallback.

## Bootstrap, recovery and format eligibility

Use the two modes and suitability rules in
[SceneTextures](scene-textures.md#exposure-hdr-domain-and-format-inventory).
New unseeded Auto, remeter, device recovery and stateless Auto use FP32 and P=1.
A borrower with only root fallback also starts FP32. Keep metering/adapting while
FP32 is retained because required products cannot fit FP16; format retention is
not a transition event.

A valid history acknowledgment is necessary but insufficient for return. Require
matching view lifetime, settings, applied/requested generation, product layout,
two consecutive eligible completed frames, half-error margin and two stops of
overflow margin. The first FP16 frame pins the qualified candidate GPU P record;
CPU does not read or compute P. Stale or delayed status cannot demote a view.

Normal-path pre-store overflow/nonfinite or required-signal underflow invalidates
metering and retains history. Schedule FP32 recovery after completed status,
without blocking. FP32 out-of-domain input reports a content/range error and
retains valid history. Use bounded status and no full-resolution telemetry target.

## Post chain and qualification

Stage 21 optionally resolves scene color. Stage 22 performs owner exposure solve,
bloom extraction/filtering when present, and tonemapping; Stage 23 extracts and
hands off the SceneRenderer-owned output. Apply final S/P once, including disabled
S=1. UI/background composition remains independent. Bloom thresholds are scene
referred. TAA/TSR slots remain future work, not implied implementation.

Owning tests: PostProcessService, ViewLifecycleService, SceneTextures,
SceneRendererDeferredCore and ShaderBakeCatalog. Use controlled float inputs
before scene integration, independent arithmetic oracles, both sharing orders,
frames in flight, failed submission, source loss and stale acknowledgments.
Captures must prove bound resources, barriers, state identities and actual final
consumption. Native game fixtures must exercise the public API without DemoShell.
