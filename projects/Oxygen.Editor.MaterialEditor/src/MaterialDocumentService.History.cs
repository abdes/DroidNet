// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Import.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Owns material history, gesture snapshots and saved-content identity under the authoring lock.</summary>
public sealed partial class MaterialDocumentService
{
    private readonly Dictionary<Guid, MaterialHistory> histories = [];

    /// <inheritdoc/>
    public MaterialEditSession BeginEditSession(Guid documentId, string field)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(field);
        lock (this.sync)
        {
            this.FinishMaterialGesture(documentId, commit: true);
            var document = this.GetDocument(documentId);
            var history = this.histories[documentId];
            var token = new MaterialEditSession(documentId, Guid.NewGuid());
            var before = CaptureMaterialProperties(documentId, document.Source);
            var group = history.Groups.Begin(token.SessionId.ToString("N"), [documentId], before, $"Edit {field}");
            history.Active = new(token, document.Source, document.CookState, group);
            return token;
        }
    }

    /// <inheritdoc/>
    public Task<MaterialEditResult> PreviewPropertiesAsync(MaterialEditSession session, PropertyEdit edit, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(edit);
        cancellationToken.ThrowIfCancellationRequested();
        lock (this.sync)
        {
            if (!this.histories.TryGetValue(session.DocumentId, out var history) || history.Active?.Token != session)
            {
                return Task.FromResult(new MaterialEditResult(Succeeded: false, OperationId: null));
            }

            var document = this.GetDocument(session.DocumentId);
            if (this.ValidatePropertyEdit(document, edit) is { } rejected)
            {
                return Task.FromResult(rejected);
            }

            var state = new MaterialEditState(document.Source);
            PropertyApply.ApplyToTarget(state, edit, MaterialDescriptors.Catalog.ById);
            if (this.ValidateEditedSource(document, state.Source) is { } invalid)
            {
                return Task.FromResult(invalid);
            }

            this.documents[document.DocumentId] = document with
            {
                Source = state.Source,
                Asset = CreateAsset(document.MaterialUri, state.Source),
                CookState = SameSource(history.Active.Before, state.Source) ? history.Active.CookState : MaterialCookState.Stale,
            };
            history.Groups.RecordPreview(history.Active.Group.Key, CaptureMaterialProperties(document.DocumentId, state.Source));
            return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }
    }

    /// <inheritdoc/>
    public MaterialEditResult CompleteEditSession(MaterialEditSession session, bool commit)
    {
        lock (this.sync)
        {
            if (this.histories.TryGetValue(session.DocumentId, out var history) && history.Active?.Token == session)
            {
                this.FinishMaterialGesture(session.DocumentId, commit);
            }

            return new(Succeeded: true, OperationId: null);
        }
    }

    /// <inheritdoc/>
    public bool CanUndo(Guid documentId)
    {
        lock (this.sync)
        {
            var history = this.histories[documentId];
            return history.Active is not null || history.Keeper.UndoStack.Count > 0;
        }
    }

    /// <inheritdoc/>
    public bool CanRedo(Guid documentId)
    {
        lock (this.sync)
        {
            return this.histories[documentId].Active is null && this.histories[documentId].Keeper.RedoStack.Count > 0;
        }
    }

    /// <inheritdoc/>
    public MaterialEditResult Undo(Guid documentId)
    {
        lock (this.sync)
        {
            if (this.histories[documentId].Active is not null)
            {
                this.FinishMaterialGesture(documentId, commit: false);
            }
            else
            {
                if (this.ValidateHistorySource(documentId, undo: true) is { } rejected)
                {
                    return rejected;
                }

                this.histories[documentId].Keeper.Undo();
            }

            return new(Succeeded: true, OperationId: null);
        }
    }

    /// <inheritdoc/>
    public MaterialEditResult Redo(Guid documentId)
    {
        lock (this.sync)
        {
            if (this.histories[documentId].Active is null)
            {
                if (this.ValidateHistorySource(documentId, undo: false) is { } rejected)
                {
                    return rejected;
                }

                this.histories[documentId].Keeper.Redo();
            }

            return new(Succeeded: true, OperationId: null);
        }
    }

    private static PropertySnapshot CaptureMaterialProperties(Guid documentId, MaterialSource source)
        => PropertySnapshot.Capture(new Dictionary<Guid, object> { [documentId] = new MaterialEditState(source) }, MaterialDescriptors.Catalog.ById.Values.ToArray());

    private static bool SameSource(MaterialSource first, MaterialSource second)
        => SerializeSource(first).AsSpan().SequenceEqual(SerializeSource(second));

    private void CommitMaterialSource(MaterialDocument document, MaterialSource source, string label)
    {
        if (SameSource(document.Source, source))
        {
            return;
        }

        var history = this.histories[document.DocumentId];
        this.documents[document.DocumentId] = document with
        {
            Source = source,
            Asset = CreateAsset(document.MaterialUri, source),
            Revision = document.Revision + 1,
            IsDirty = !SameSource(history.SavedSource, source),
            CookState = MaterialCookState.Stale,
        };
        this.RecordMaterialHistory(document.DocumentId, document.Source, label);
    }

    private MaterialEditResult? ValidateHistorySource(Guid documentId, bool undo)
    {
        var keeper = this.histories[documentId].Keeper;
        var entry = undo ? keeper.UndoStack.LastOrDefault() : keeper.RedoStack.FirstOrDefault();
        return entry?.Key is MaterialHistoryKey key ? this.ValidateEditedSource(this.GetDocument(documentId), key.Source) : null;
    }

    private void RecordMaterialHistory(Guid documentId, MaterialSource restore, string label)
        => this.histories[documentId].Keeper.AddChange(new SimpleAction(() =>
        {
            lock (this.sync)
            {
                // Undo/redo validates the snapshot before popping history, under this same lock.
                this.CommitMaterialSource(this.GetDocument(documentId), restore, label);
            }
        }) { Key = new MaterialHistoryKey(label, restore) });

    private void FinishMaterialGesture(Guid documentId, bool commit)
    {
        if (!this.histories.TryGetValue(documentId, out var history) || history.Active is not { } gesture)
        {
            return;
        }

        history.Active = null;
        var document = this.GetDocument(documentId);
        var after = CaptureMaterialProperties(documentId, document.Source);
        _ = history.Groups.Close(gesture.Group.Key, after);
        var operation = new PropertyOp([documentId], gesture.Group.Before, after, gesture.Group.Label);
        var before = document with
        {
            Source = gesture.Before,
            Asset = CreateAsset(document.MaterialUri, gesture.Before),
            IsDirty = !SameSource(history.SavedSource, gesture.Before),
            CookState = gesture.CookState,
        };
        this.documents[documentId] = before;
        if (commit && operation.EffectiveEdit().Count > 0)
        {
            this.CommitMaterialSource(before, document.Source, gesture.Group.Label);
        }
    }

    private sealed class MaterialHistory
    {
        public MaterialHistory(MaterialSource savedSource)
        {
            this.SavedSource = savedSource;
            this.Keeper = new(this);
        }

        public HistoryKeeper Keeper { get; }

        public CommitGroupController Groups { get; } = new();

        public MaterialSource SavedSource { get; set; }

        public MaterialGesture? Active { get; set; }
    }

    private sealed record MaterialGesture(MaterialEditSession Token, MaterialSource Before, MaterialCookState CookState, CommitGroupSession Group);

    private sealed record MaterialHistoryKey(string Label, MaterialSource Source)
    {
        public override string ToString() => this.Label;
    }
}
