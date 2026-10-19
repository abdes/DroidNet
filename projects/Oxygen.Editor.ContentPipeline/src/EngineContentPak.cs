// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>The SDK's engine content pak, read from the file the installation ships.</summary>
/// <param name="path">The installation's <c>share/oxygen/engine.pak</c>.</param>
public sealed class EngineContentPak(FileSystemPath path) : IEngineContentPak
{
    /// <inheritdoc />
    public FileSystemPath Path { get; } = path ?? throw new ArgumentNullException(nameof(path));

    /// <summary>Returns the engine content pak of the installation this process runs from.</summary>
    /// <returns>The pak the installation ships; it may be missing, which reads report.</returns>
    public static EngineContentPak Installed()
        => new(FileSystemPath.From(EditorNativeInstallation.Discover(AppContext.BaseDirectory, EditorNativeCompatibilityService.CurrentConfiguration).EngineContentPakPath));

    /// <inheritdoc />
    public Task<PakDocument> ReadAsync(CancellationToken cancellationToken) => PakIndex.ReadFileAsync(this.Path, cancellationToken);
}
