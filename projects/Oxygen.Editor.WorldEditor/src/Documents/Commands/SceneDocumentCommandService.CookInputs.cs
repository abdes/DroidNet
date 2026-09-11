// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Protects the saved scene source while cooking captures it without completing authoring gestures.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    public async Task<CookDocumentReadLease?> AcquireCookReadAsync(SceneDocumentCommandContext context, CancellationToken cancellationToken)
    {
        var owner = EnterAuthoring(context);
        if (owner is null)
        {
            return null;
        }

        var gate = SaveGates.GetValue(context.Scene, static _ => new SemaphoreSlim(1, 1));
        var acquired = false;
        var transferred = false;
        try
        {
            await gate.WaitAsync(cancellationToken).ConfigureAwait(true);
            acquired = true;
            if (!ReferenceEquals(this.sceneEngineSync.GetDocumentScene(context.Metadata), context.Scene))
            {
                return null;
            }

            var source = this.projectManager.GetSceneSourceVersion(context.Scene)
                ?? throw new InvalidOperationException("The scene has no acknowledged saved source.");
            var dirty = context.Metadata.IsDirty || this.propertyGestures.Values.Any(gesture =>
                ReferenceEquals(gesture.Context.Scene, context.Scene)
                && ReferenceEquals(gesture.Context.Metadata, context.Metadata)
                && HasChangedPreview(gesture));
            var state = new CookDocumentState(
                context.DocumentId,
                Path.GetFullPath(source.SourcePath),
                context.Metadata.Title,
                context.Metadata.ChangeVersion,
                context.Metadata.SavedVersion,
                dirty,
                source.Version.Sha256);
            var lease = new CookDocumentReadLease(state, () =>
            {
                _ = gate.Release();
                owner.Dispose();
            });
            transferred = true;
            return lease;
        }
        finally
        {
            if (!transferred)
            {
                if (acquired)
                {
                    _ = gate.Release();
                }

                owner.Dispose();
            }
        }
    }

    private static bool HasChangedPreview(PropertyGesture gesture)
    {
        var current = PropertySnapshot.Capture(GestureTargets(gesture.Context, gesture.Kind, gesture.Nodes), gesture.Descriptors);
        return new PropertyOp(gesture.Nodes, gesture.Before, current, gesture.Label).EffectiveEdit().Count > 0;
    }
}
