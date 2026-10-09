// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

internal sealed class FakeEngineSession : EngineSession
{
    private bool hasRunner;
    private bool hasContext;

    public override int LoggingVerbosity { get; set; }

    public override uint MaxTargetFps => 1000;

    public override uint TargetFps { get; set; }

    public bool VSyncEnabled { get; private set; } = true;

    public bool AlwaysRenderPanes { get; private set; }

    public override IRuntimeCommandTransport Commands { get; } = Moq.Mock.Of<IRuntimeCommandTransport>();

    public override bool HasRunner => this.hasRunner;

    public override bool HasContext => this.hasContext;

    public List<string> Calls { get; } = [];

    public TaskCompletionSource Loop { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public Func<ViewportSurfaceKey, Task<bool>> Register { get; set; } = _ => Task.FromResult(true);

    public Func<Guid, Task<bool>> Unregister { get; set; } = _ => Task.FromResult(true);

    public Task Cleanup { get; set; } = Task.CompletedTask;

    public Task Startup { get; set; } = Task.CompletedTask;

    public string? FailAt { get; set; }

    public bool CompleteOnStop { get; set; } = true;

    public override void Initialize(IEngineSettings settings, string? editorCVarsArchivePath, ILogger? logger)
    {
        this.hasRunner = true;
        this.Step("Create runner");
        this.hasContext = true;
        this.Step("Create context");
    }

    public override Task RunAsync()
    {
        this.Step("Run");
        return this.Loop.Task;
    }

    public override Task WaitForStartupAsync() => this.Startup;

    public override void Stop()
    {
        this.Step("Stop");
        if (this.CompleteOnStop)
        {
            _ = this.Loop.TrySetResult();
        }
    }

    public override void SetVSyncEnabled(bool enabled) => this.VSyncEnabled = enabled;

    public override void SetAlwaysRenderPanes(bool alwaysRender) => this.AlwaysRenderPanes = alwaysRender;

    public override Task CompleteLoopCleanupAsync()
    {
        this.Step("Loop cleanup");
        return this.Cleanup;
    }

    public override Task<bool> RegisterSurfaceAsync(ViewportSurfaceKey key, SwapChainPanel panel)
    {
        this.Step("Register");
        return this.Register(key);
    }

    public override Task<bool> UnregisterSurfaceAsync(Guid viewportId)
    {
        this.Step("Unregister");
        return this.Unregister(viewportId);
    }

    public override Task<bool> ResizeSurfaceAsync(Guid viewportId, uint width, uint height) => Task.FromResult(true);

    public override Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config) => Task.FromResult(new RuntimeViewId(1));

    public override Task<bool> DestroyViewAsync(RuntimeViewId viewId) => Task.FromResult(true);

    public override Task<bool> ShowViewAsync(RuntimeViewId viewId) => Task.FromResult(true);

    public override Task<bool> HideViewAsync(RuntimeViewId viewId) => Task.FromResult(true);

    public override Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset) => Task.FromResult(true);

    public override Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode) => Task.FromResult(true);

    public override Task<bool> SetViewSceneCameraAsync(RuntimeViewId viewId, Guid? cameraNodeId) => Task.FromResult(true);

    public override Task<bool> SetViewScenePilotAsync(RuntimeViewId viewId, bool pilot) => Task.FromResult(true);

    public override Task<RuntimeViewCameraPose?> GetViewCameraPoseAsync(RuntimeViewId viewId, Guid nodeId) => Task.FromResult<RuntimeViewCameraPose?>(null);

    public override Task<RuntimeEditorCamera?> GetViewEditorCameraAsync(RuntimeViewId viewId) => Task.FromResult<RuntimeEditorCamera?>(null);

    public override Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond) => Task.FromResult(true);

    public override Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane) => Task.FromResult(true);

    public override Task<bool> SetViewRenderOptionsAsync(RuntimeViewId viewId, ViewportRenderOptions options) => Task.FromResult(true);

    public override Task<RuntimePickResult?> PickViewAsync(RuntimeViewId viewId, RuntimePickRect rect) => Task.FromResult<RuntimePickResult?>(null);

    public override Task<RuntimeFramingOutcome> FrameViewAsync(RuntimeViewId viewId, IReadOnlyList<Guid> nodeIds) => Task.FromResult(RuntimeFramingOutcome.Framed);

    public override bool SetSelectionOutline(IReadOnlyList<Guid> nodeIds, Guid? activeNodeId) => true;

    public override bool SetTransformGizmo(RuntimeTransformGizmo gizmo) => true;

    public override bool CancelTransformGizmoDrag() => true;

    public override bool SetSceneHelpers(RuntimeSceneHelpers helpers) => true;

    public override bool SetTransformGizmoListener(Action<RuntimeGizmoEvent>? listener) => true;

    public override RuntimeFrameStatistics GetFrameStatistics() => default;

    public override void DestroyContext()
    {
        this.Step("Destroy context");
        this.hasContext = false;
    }

    public override void DestroyRunner()
    {
        this.Step("Destroy runner");
        this.hasRunner = false;
    }

    private void Step(string step)
    {
        this.Calls.Add(step);
        if (string.Equals(this.FailAt, step, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(step);
        }
    }
}
