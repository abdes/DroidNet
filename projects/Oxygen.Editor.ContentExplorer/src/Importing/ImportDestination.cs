// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Importing;

/// <summary>Reads the destination folder people type in an import review.</summary>
internal static class ImportDestination
{
    /// <summary>Reads a typed destination, such as <c>Content/Models</c> or <c>/Content/Models/</c>.</summary>
    /// <param name="folder">The typed folder.</param>
    /// <returns>The folder.</returns>
    /// <exception cref="ArgumentException">The text names no folder.</exception>
    public static AssetPath Parse(string folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        return AssetPath.Parse("/" + folder.Trim().Trim('/'));
    }
}
