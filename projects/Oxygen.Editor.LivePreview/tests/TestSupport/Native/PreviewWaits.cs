// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport.Native;

/// <summary>Waits for what the real engine shows.</summary>
internal static class PreviewWaits
{
    /// <summary>Waits until the engine shows a scene document's current content.</summary>
    /// <param name="preview">The preview.</param>
    /// <param name="document">The scene document.</param>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>The wait.</returns>
    public static async Task UntilReadyAsync(IScenePreview preview, DocumentId document, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(preview);
        using var changed = new SemaphoreSlim(0);
        void OnReadyChanged(object? sender, ScenePreviewEventArgs args) => changed.Release();
        preview.ReadyChanged += OnReadyChanged;
        try
        {
            while (!preview.IsReady(document))
            {
                await changed.WaitAsync(cancellationToken).ConfigureAwait(true);
            }
        }
        finally
        {
            preview.ReadyChanged -= OnReadyChanged;
        }
    }

    /// <summary>Observes a node of the shown scene until its state satisfies a condition; the engine applies edits on its frame.</summary>
    /// <param name="engine">The engine.</param>
    /// <param name="target">The activation that shows the scene.</param>
    /// <param name="node">The node.</param>
    /// <param name="condition">The state to wait for.</param>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>The observed state.</returns>
    public static async Task<RuntimeNodeState> UntilNodeAsync(IEngineService engine, RuntimeSceneTarget target, NodeId node, Func<RuntimeNodeState, bool> condition, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(engine);
        ArgumentNullException.ThrowIfNull(condition);
        while (true)
        {
            var observed = await engine.WorldCommands.ObserveNodeAsync(Guid.NewGuid(), target, node, cancellationToken).ConfigureAwait(true);
            if (observed.Outcome.Succeeded && observed.State is { Exists: true } state && condition(state))
            {
                return state;
            }

            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }
    }
}
