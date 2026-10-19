// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.Materials.Documents;

/// <summary>
///     The content of a material document: the <see cref="Materials.Material"/> being edited, and the edit gesture in
///     progress, if any.
/// </summary>
/// <remarks>
///     <see cref="MaterialEdits"/> changes the material and records each change in the document's history. A gesture,
///     such as dragging a slider, previews its values without recording and becomes one history entry when it finishes.
/// </remarks>
public sealed class MaterialContent : IDocumentContent
{
    private readonly IDocumentKind kind;
    private readonly MaterialValidation validation;

    internal MaterialContent(IDocumentKind kind, MaterialSource source, MaterialValidation validation)
    {
        this.kind = kind;
        this.Material = new Material(source);
        this.validation = validation;
    }

    /// <summary>
    ///     Gets the material being edited.
    /// </summary>
    public Material Material { get; }

    /// <inheritdoc/>
    public bool HasEditsInProgress => this.Gesture is not null;

    /// <summary>
    ///     Gets or sets the edit gesture in progress.
    /// </summary>
    internal MaterialGesture? Gesture { get; set; }

    /// <inheritdoc/>
    /// <remarks>A gesture in progress is committed as one history entry.</remarks>
    public Task FinishEditsAsync(CancellationToken cancellationToken)
    {
        this.Gesture?.Finish(commit: true);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    /// <remarks>A source that does not match the engine's material schema is refused.</remarks>
    public DocumentCapture Capture() => this.CaptureOf(this.Material.Source);

    /// <inheritdoc/>
    /// <remarks>The copy's material is named after the copy's file.</remarks>
    public DocumentCapture CaptureCopy(DocumentFile copy)
    {
        ArgumentNullException.ThrowIfNull(copy);
        return this.CaptureOf(this.Material.Source with { Name = DocumentKinds.TitleOf(this.kind, copy.Path) });
    }

    /// <inheritdoc/>
    /// <remarks>Texture references follow the relocation; the material takes its file's new name.</remarks>
    public void ApplyRelocation(DocumentRelocation relocation)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        var retargeted = Material.Retarget(this.Material.Source, relocation.MapAsset);
        this.Material.Replace(retargeted with { Name = DocumentKinds.TitleOf(this.kind, relocation.File.Path) });
    }

    private DocumentCapture CaptureOf(MaterialSource source)
        => this.validation.ValidateForSave(source) is { } problem
            ? DocumentCapture.Refused(problem.Message)
            : DocumentCapture.Of(_ => Task.FromResult<ReadOnlyMemory<byte>>(Material.Serialize(source)));
}
