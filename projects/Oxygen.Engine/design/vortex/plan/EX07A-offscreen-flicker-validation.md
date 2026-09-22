# Offscreen lighting flicker: same-frame descriptor lifetime

On 2026-09-22 the user observed alternating lit/black geometry in MultiView's
lower offscreen panes in Release, with the grid still visible. The reproduction
used `--directional-array-proof true --offscreen-proof-layout true
--pip-wireframe false -v=-1` in the pending directional-array checkpoint.

`ValidatedOffscreenSceneSession::ExecuteInsideFrame` restarts scene preparation
for offscreen execution and restores the main scene preparation afterward.
Those starts use the same physical frame sequence and slot.
`TransientStructuredBuffer::OnFrameStart` previously unregistered that slot's
SRVs every time, including descriptors referenced by earlier queued draws.
Their reuse could substitute a later lighting publication; generation checks
then rejected those inputs and the geometry received no lighting.

The allocator now preserves allocations when both sequence and slot are
unchanged. Advancing the slot to a different sequence still retires its old
descriptors under the existing caller-owned frame-fence contract.

The committed regression starts frame zero, publishes an SRV, repeats the same
frame start, and checks the original registry entry remains live while another
allocation is published. It also checks normal retirement on sequence advance.
It failed before the fix with the original SRV absent from the registry.
All **13 transient-buffer tests pass in Debug and Release** after the fix.
Oxytidy covered the changed source, header and test with no failed contexts;
its six remaining findings concern unchanged code. No suppression was added.

The user reviewed the Ninja Release application with the exact reproduction
options and confirmed: **both lower views are stable**. This live check matters
because capture instrumentation can change the timing of premature descriptor
reuse. Logs and test JSON are under
`out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/repeated-frame-*`.

This closes the reported flicker and same-frame descriptor-reset defect. It is
not a blanket qualification of all upload, deferred-CBV or shadow-resource
lifetime paths in EX07.
