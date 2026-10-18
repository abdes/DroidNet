// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>
/// Creates scripts, input files and scenes from their templates: proposes a folder and a free name, checks the name the
/// user chose, and writes the file without overwriting anything.
/// </summary>
/// <remarks>
/// A name collides with any file of the same kind and name in the folder, whatever its extension, because both would
/// cook to the same asset. The check reads the asset catalog; a file the catalog has not seen yet is still never
/// overwritten, because the file is created only where none exists.
/// </remarks>
public interface IAssetAuthoring
{
    /// <summary>
    /// Occurs when a file was created, with the file in <see cref="AssetFilesChangedEventArgs.CreatedFiles"/>. It is
    /// raised on the synchronization context of the caller of <see cref="CreateAsync"/>, before that call completes, so
    /// a listener has seen the file before its caller does.
    /// </summary>
    public event EventHandler<AssetFilesChangedEventArgs>? FilesChanged;

    /// <summary>Proposes the folder and the first free name for a new file.</summary>
    /// <param name="project">The project.</param>
    /// <param name="kind">What to create.</param>
    /// <param name="selectedFolder">The folder being browsed, a virtual path such as <c>/Content/Player</c>, if any.
    /// A mount root is replaced by the kind's folder under it.</param>
    /// <param name="cancellationToken">Cancels reading the catalog.</param>
    /// <returns>The proposal.</returns>
    public Task<AssetProposal> ProposeAsync(ProjectContext project, AuthoredAssetKind kind, AssetPath? selectedFolder, CancellationToken cancellationToken);

    /// <summary>Checks a name and folder for a new file.</summary>
    /// <param name="project">The project.</param>
    /// <param name="kind">What to create.</param>
    /// <param name="name">The name, without extension.</param>
    /// <param name="folder">The virtual folder.</param>
    /// <param name="cancellationToken">Cancels reading the catalog.</param>
    /// <returns>Why the name is rejected, or <see langword="null"/> when it is accepted.</returns>
    public Task<string?> ValidateAsync(ProjectContext project, AuthoredAssetKind kind, string name, AssetPath folder, CancellationToken cancellationToken);

    /// <summary>Checks the name and folder again and writes the template there.</summary>
    /// <param name="project">The project.</param>
    /// <param name="kind">What to create.</param>
    /// <param name="name">The name, without extension.</param>
    /// <param name="folder">The virtual folder.</param>
    /// <param name="cancellationToken">Cancels before the file is written.</param>
    /// <returns>The absolute path of the new file.</returns>
    /// <exception cref="AssetCreationException">The name is rejected, a file already exists there, or the file
    /// could not be written; nothing was created.</exception>
    public Task<FileSystemPath> CreateAsync(ProjectContext project, AuthoredAssetKind kind, string name, AssetPath folder, CancellationToken cancellationToken);
}
