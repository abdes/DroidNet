// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine.Changes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Materials.Documents;

/// <summary>
///     Edits material documents: each accepted change replaces the material's source and records one entry, whose undo
///     restores the previous source, in the document's history.
/// </summary>
/// <remarks>
///     <para>
///     An edit is validated against the material's property descriptors and the edited source against the material
///     schemas; a rejected edit changes nothing and is published as a failed operation. A gesture previews its values
///     without recording, and its end records one entry or restores the source it began with.
///     </para>
///     <para>
///     Undo and redo check the source they restore first. A source captured before a relocation refers to textures by
///     the paths they had then; restoring it follows the textures to their current paths, and warns about a texture that
///     was deleted meanwhile.
///     </para>
/// </remarks>
/// <param name="validation">Checks edited sources against the material schemas.</param>
/// <param name="operationResults">Publishes rejected edits and restored references to deleted textures, if present.</param>
/// <param name="redirects">Follows textures captured before a relocation to their current paths, if present.</param>
/// <param name="loggerFactory">The logger factory, if any.</param>
public sealed partial class MaterialEdits(
    MaterialValidation validation,
    IOperationResultPublisher? operationResults = null,
    IAssetRedirects? redirects = null,
    ILoggerFactory? loggerFactory = null)
{
    private const string EditLabel = "Edit Material";

    private readonly ILogger logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<MaterialEdits>();

    private static MaterialEditResult Accepted { get; } = new(Succeeded: true, OperationId: null);

    /// <summary>
    ///     Applies a property edit to a material.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <param name="edit">The values to set.</param>
    /// <returns>Whether the edit was accepted, and the published failure when it was not.</returns>
    public MaterialEditResult Apply(Document document, PropertyEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        var content = ContentOf(document);
        content.Gesture?.Finish(commit: true);
        if (edit.Count == 0)
        {
            return Accepted;
        }

        if (this.Validate(document, edit) is { } rejected)
        {
            return rejected;
        }

        var state = new MaterialEditState(content.Material.Source);
        PropertyApply.ApplyToTarget(state, edit, MaterialDescriptors.Catalog.ById);
        if (this.ValidateEdited(document, state.Source) is { } invalid)
        {
            return invalid;
        }

        this.Commit(document, content, state.Source, EditLabel);
        this.LogPropertiesApplied(document.Title, edit.Count);
        return Accepted;
    }

    /// <summary>
    ///     Sets or clears the texture of one of a material's channels.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <param name="channel">The texture channel.</param>
    /// <param name="virtualPath">The texture's canonical absolute virtual path, or <see langword="null"/> to clear it.</param>
    /// <returns>Whether the change was accepted, and the published failure when it was not.</returns>
    public MaterialEditResult SetTexture(Document document, string channel, string? virtualPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channel);
        var content = ContentOf(document);
        content.Gesture?.Finish(commit: true);
        if (Material.CheckTexture(channel, virtualPath) is { } code)
        {
            return this.Reject(document, code, $"The {channel} texture cannot be '{virtualPath}'.");
        }

        var updated = content.Material.Source.WithTextureReference(channel, virtualPath);
        if (this.ValidateEdited(document, updated) is { } invalid)
        {
            return invalid;
        }

        this.Commit(document, content, updated, $"Edit {channel} texture");
        return Accepted;
    }

    /// <summary>
    ///     Begins an edit gesture, ending the one in progress first.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <param name="field">The field the gesture edits, which labels its history entry.</param>
    /// <returns>The gesture's session.</returns>
    public MaterialEditSession BeginEditSession(Document document, string field)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(field);
        var content = ContentOf(document);
        content.Gesture?.Finish(commit: true);
        var session = new MaterialEditSession(document.Id, Guid.NewGuid());
        MaterialGesture? gesture = null;
        gesture = new MaterialGesture(session, $"Edit {field}", content.Material.Source, commit => this.FinishGesture(document, content, gesture!, commit));
        content.Gesture = gesture;
        return session;
    }

    /// <summary>
    ///     Previews values during a gesture, without recording them.
    /// </summary>
    /// <param name="document">The material document the gesture edits.</param>
    /// <param name="session">The gesture's session.</param>
    /// <param name="edit">The values to preview.</param>
    /// <returns>Whether the preview was accepted; a preview of an ended gesture is not.</returns>
    public MaterialEditResult Preview(Document document, MaterialEditSession session, PropertyEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        var content = ContentOf(document);
        if (content.Gesture?.Session != session)
        {
            return new MaterialEditResult(Succeeded: false, OperationId: null);
        }

        if (this.Validate(document, edit) is { } rejected)
        {
            return rejected;
        }

        var state = new MaterialEditState(content.Material.Source);
        PropertyApply.ApplyToTarget(state, edit, MaterialDescriptors.Catalog.ById);
        if (this.ValidateEdited(document, state.Source) is { } invalid)
        {
            return invalid;
        }

        content.Material.Replace(state.Source);
        return Accepted;
    }

    /// <summary>
    ///     Ends a gesture: commits its last preview as one history entry, or restores the source it began with.
    /// </summary>
    /// <param name="document">The material document the gesture edits.</param>
    /// <param name="session">The gesture's session; a gesture that already ended is ignored.</param>
    /// <param name="commit">Whether to keep the gesture's last preview.</param>
    public void CompleteEditSession(Document document, MaterialEditSession session, bool commit)
    {
        if (ContentOf(document).Gesture is { } gesture && gesture.Session == session)
        {
            gesture.Finish(commit);
        }
    }

    /// <summary>
    ///     Tells whether Undo has something to undo: a gesture in progress, or the document's history.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <returns>Whether Undo has something to undo.</returns>
    public bool CanUndo(Document document) => ContentOf(document).Gesture is not null || document.History.CanUndo;

    /// <summary>
    ///     Tells whether Redo has something to redo.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <returns>Whether Redo has something to redo.</returns>
    public bool CanRedo(Document document) => ContentOf(document).Gesture is null && document.History.CanRedo;

    /// <summary>
    ///     Cancels the gesture in progress, or undoes the document's last history entry when the source it restores is
    ///     valid.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <returns>Whether the undo was done, and the published failure when it was refused.</returns>
    public MaterialEditResult Undo(Document document)
    {
        var content = ContentOf(document);
        if (content.Gesture is { } gesture)
        {
            gesture.Finish(commit: false);
            return Accepted;
        }

        if (this.ValidateRestored(document, document.History.UndoStack.LastOrDefault()?.Key) is { } rejected)
        {
            return rejected;
        }

        return document.History.Undo() ? Accepted : new MaterialEditResult(Succeeded: false, OperationId: null);
    }

    /// <summary>
    ///     Redoes the document's next history entry when the source it restores is valid.
    /// </summary>
    /// <param name="document">A material document.</param>
    /// <returns>Whether the redo was done, and the published failure when it was refused.</returns>
    public MaterialEditResult Redo(Document document)
    {
        if (ContentOf(document).Gesture is not null)
        {
            return new MaterialEditResult(Succeeded: false, OperationId: null);
        }

        if (this.ValidateRestored(document, document.History.RedoStack.FirstOrDefault()?.Key) is { } rejected)
        {
            return rejected;
        }

        return document.History.Redo() ? Accepted : new MaterialEditResult(Succeeded: false, OperationId: null);
    }

    private static MaterialContent ContentOf(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return document.Content as MaterialContent
            ?? throw new ArgumentException($"'{document.Title}' is not a material document.", nameof(document));
    }

    // One entry per accepted change; undoing it records the inverse, which the history routes to the other stack.
    private void Commit(Document document, MaterialContent content, MaterialSource source, string label)
    {
        var previous = content.Material.Source;
        if (Material.AreSame(previous, source))
        {
            return;
        }

        content.Material.Replace(source);
        document.History.AddChange(new SimpleAction(() => this.Restore(document, previous, label))
        {
            Key = new MaterialHistoryKey(label, previous),
        });
    }

    private void Restore(Document document, MaterialSource source, string label)
        => this.Commit(document, ContentOf(document), this.FollowRedirects(document, source, warn: true), label);

    private void FinishGesture(Document document, MaterialContent content, MaterialGesture gesture, bool commit)
    {
        if (!ReferenceEquals(content.Gesture, gesture))
        {
            return;
        }

        content.Gesture = null;
        var last = content.Material.Source;
        content.Material.Replace(gesture.Before);
        if (commit)
        {
            this.Commit(document, content, last, gesture.Label);
        }
    }

    private MaterialEditResult? ValidateRestored(Document document, object? key)
        => key is MaterialHistoryKey restored
            ? this.ValidateEdited(document, this.FollowRedirects(document, restored.Source, warn: false))
            : null;

    private MaterialEditResult? Validate(Document document, PropertyEdit edit)
    {
        var descriptors = MaterialDescriptors.Catalog.ById;
        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                return this.Reject(document, MaterialDiagnosticCodes.PropertyUnknown, $"Unknown property id: {id.Qualified()}.");
            }

            var check = descriptor.ValidateBoxed(value);
            if (!check.IsValid)
            {
                return this.Reject(document, check.Code, check.Message);
            }
        }

        return null;
    }

    private MaterialEditResult? ValidateEdited(Document document, MaterialSource source)
        => validation.ValidateEdited(source) is { } problem
            ? this.Reject(document, problem.Code, string.IsNullOrEmpty(problem.Explanation) ? problem.Message : $"{problem.Message} {problem.Explanation}")
            : null;

    private MaterialEditResult Reject(Document document, string code, string message)
    {
        this.LogEditRejected(document.Title, code, message);
        var operationId = this.Publish(
            document,
            OperationStatus.Failed,
            DiagnosticSeverity.Error,
            code,
            "Material property edit was rejected.",
            message);
        return new MaterialEditResult(Succeeded: false, OperationId: operationId, code);
    }

    private MaterialSource FollowRedirects(Document document, MaterialSource source, bool warn)
    {
        if (redirects is null)
        {
            return source;
        }

        var resolved = Material.Retarget(source, path =>
        {
            var current = redirects.Resolve(path);
            return string.Equals(current, path, StringComparison.Ordinal) ? null : current;
        });
        if (!warn)
        {
            return resolved;
        }

        // Warn only for textures the step brings back; one the material already shows as missing was reported before.
        var shown = ContentOf(document).Material.Source.TextureReferences.Values.ToHashSet(StringComparer.OrdinalIgnoreCase);
        var deleted = resolved.TextureReferences.Values
            .Where(path => !shown.Contains(path) && AssetPath.TryParse(path, out var texture) && redirects.WasDeleted(texture))
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (deleted.Length != 0)
        {
            var names = string.Join(", ", deleted.Select(Path.GetFileName));
            this.LogRestoredDeletedTexture(document.Title, names);
            _ = this.Publish(
                document,
                OperationStatus.SucceededWithWarnings,
                DiagnosticSeverity.Warning,
                MaterialDiagnosticCodes.TextureRestoredDeleted,
                "Restored a reference to a deleted texture",
                $"{names} was deleted, so the restored reference shows as missing until you choose another texture.");
        }

        return resolved;
    }

    private Guid? Publish(Document document, OperationStatus status, DiagnosticSeverity severity, string code, string title, string message)
    {
        if (operationResults is null)
        {
            return null;
        }

        var operationId = Guid.NewGuid();
        var virtualPath = document.File.Asset.VirtualPath;
        var scope = new AffectedScope
        {
            AssetId = document.File.Asset.ToUriString(),
            AssetSourcePath = document.File.Path.FullPath,
            AssetVirtualPath = virtualPath,
        };
        operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = MaterialOperationKinds.EditScalar,
            Status = status,
            Severity = severity,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = scope,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.MaterialAuthoring,
                    Severity = severity,
                    Code = code,
                    Message = message,
                    AffectedPath = document.File.Path.FullPath,
                    AffectedVirtualPath = virtualPath,
                    AffectedEntity = scope,
                },
            ],
        });
        return operationId;
    }

    private sealed record MaterialHistoryKey(string Label, MaterialSource Source)
    {
        public override string ToString() => this.Label;
    }
}
