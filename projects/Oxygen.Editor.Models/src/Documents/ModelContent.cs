// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models.Documents;

/// <summary>What a model document shows: the model, named by its source file.</summary>
/// <remarks>
///     The model's structure and values are read-only. The scene that previews it is built by the scenes module from
///     <see cref="Source"/>, as any scene instances a model.
/// </remarks>
public sealed class ModelContent : IDocumentContent
{
    /// <summary>Initializes a new instance of the <see cref="ModelContent"/> class.</summary>
    /// <param name="source">The model's source file, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    public ModelContent(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.Source = source;
    }

    /// <summary>Occurs when the model's source file moved or was renamed, after <see cref="Source"/> follows it.</summary>
    public event EventHandler<ModelRelocatedEventArgs>? Relocated;

    /// <summary>Gets the model's source file, which identifies the model and is what a scene places.</summary>
    public AssetPath Source { get; private set; }

    /// <inheritdoc/>
    public bool HasEditsInProgress => false;

    /// <inheritdoc/>
    public Task FinishEditsAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <inheritdoc/>
    public DocumentCapture Capture() => DocumentCapture.Refused($"'{this.Source.Name}' is a model source; the editor never writes it.");

    /// <inheritdoc/>
    public DocumentCapture CaptureCopy(DocumentFile copy) => DocumentCapture.Refused("A model is copied as its source file, outside the editor.");

    /// <inheritdoc/>
    /// <remarks>The model follows its source file to its new name.</remarks>
    public void ApplyRelocation(DocumentRelocation relocation)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        this.Source = relocation.File.Asset;
        this.Relocated?.Invoke(this, new ModelRelocatedEventArgs(relocation));
    }
}
