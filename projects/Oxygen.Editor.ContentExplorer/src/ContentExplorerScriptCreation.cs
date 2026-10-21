// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentExplorer.Authoring;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>Creates scripts through the window's Content Explorer, which asks for the name and folder.</summary>
public sealed class ContentExplorerScriptCreation : IScriptCreation
{
    private AuthoredAssetCreation? creation;

    /// <inheritdoc />
    public bool IsAvailable => this.creation is not null;

    /// <inheritdoc />
    public Task<FileSystemPath?> CreateScriptAsync()
        => this.creation is { } current ? current.CreateScriptAsync() : Task.FromResult<FileSystemPath?>(null);

    /// <summary>Makes a Content Explorer's creation the one that creates scripts.</summary>
    /// <param name="value">The creation of the explorer, which the window's router created.</param>
    internal void Attach(AuthoredAssetCreation value) => this.creation = value;

    /// <summary>Forgets the creation of a Content Explorer that ended.</summary>
    /// <param name="value">The creation.</param>
    internal void Detach(AuthoredAssetCreation value)
    {
        if (ReferenceEquals(this.creation, value))
        {
            this.creation = null;
        }
    }
}
