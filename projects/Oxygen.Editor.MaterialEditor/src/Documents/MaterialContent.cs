// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
///     The content of a material document: the material's current source, and the edit gesture in progress, if any.
/// </summary>
/// <remarks>
///     <see cref="MaterialEdits"/> changes the source and records each change in the document's history. A gesture, such
///     as dragging a slider, previews its values without recording and becomes one history entry when it finishes.
/// </remarks>
public sealed class MaterialContent : IDocumentContent
{
    private readonly IDocumentKind kind;
    private readonly MaterialValidation validation;

    internal MaterialContent(IDocumentKind kind, MaterialSource source, MaterialValidation validation)
    {
        this.kind = kind;
        this.Source = source;
        this.validation = validation;
    }

    /// <summary>
    ///     Raised after <see cref="Source"/> changes, on the thread that changed it.
    /// </summary>
    public event EventHandler? SourceChanged;

    /// <summary>
    ///     Gets the material's current source.
    /// </summary>
    public MaterialSource Source { get; private set; }

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
    public DocumentCapture Capture() => this.CaptureOf(this.Source);

    /// <inheritdoc/>
    /// <remarks>The copy's material is named after the copy's file.</remarks>
    public DocumentCapture CaptureCopy(DocumentFile copy)
    {
        ArgumentNullException.ThrowIfNull(copy);
        return this.CaptureOf(this.Source with { Name = DocumentKinds.TitleOf(this.kind, copy.Path) });
    }

    /// <inheritdoc/>
    /// <remarks>Texture references follow the relocation; the material takes its file's new name.</remarks>
    public void ApplyRelocation(DocumentRelocation relocation)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        var retargeted = Retarget(this.Source, relocation.MapReference);
        this.Replace(retargeted with { Name = DocumentKinds.TitleOf(this.kind, relocation.File.Path) });
    }

    /// <summary>
    ///     Gives each texture reference the value a map returns for it, and keeps the references it returns no value for.
    /// </summary>
    /// <param name="source">The source whose references are mapped.</param>
    /// <param name="map">Maps a texture's virtual path, or returns <see langword="null"/> to keep it.</param>
    /// <returns>The source with its references mapped.</returns>
    internal static MaterialSource Retarget(MaterialSource source, Func<string, string?> map)
    {
        foreach (var (channel, path) in source.TextureReferences.ToArray())
        {
            if (map(path) is { } mapped)
            {
                source = source.WithTextureReference(channel, mapped);
            }
        }

        return source;
    }

    /// <summary>
    ///     Tells whether two sources would be written the same.
    /// </summary>
    /// <param name="first">The first source.</param>
    /// <param name="second">The second source.</param>
    /// <returns>Whether the sources serialize to the same bytes.</returns>
    internal static bool SameSource(MaterialSource first, MaterialSource second)
        => ReferenceEquals(first, second) || Serialize(first).AsSpan().SequenceEqual(Serialize(second));

    /// <summary>
    ///     Replaces the source, and raises <see cref="SourceChanged"/> when it differs.
    /// </summary>
    /// <param name="source">The new source.</param>
    internal void Replace(MaterialSource source)
    {
        if (ReferenceEquals(this.Source, source))
        {
            return;
        }

        this.Source = source;
        this.SourceChanged?.Invoke(this, EventArgs.Empty);
    }

    private static byte[] Serialize(MaterialSource source)
    {
        using var memory = new MemoryStream();
        MaterialSourceWriter.Write(memory, source);
        return memory.ToArray();
    }

    private DocumentCapture CaptureOf(MaterialSource source)
        => this.validation.ValidateForSave(source) is { } problem
            ? DocumentCapture.Refused(problem)
            : DocumentCapture.Of(_ => Task.FromResult<ReadOnlyMemory<byte>>(Serialize(source)));
}
