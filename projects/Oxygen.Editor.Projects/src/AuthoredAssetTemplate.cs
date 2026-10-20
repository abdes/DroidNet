// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
/// What one kind of new file is called, where it goes in a project and what it starts with. Each asset family owns the
/// template of its files; the creation workflow takes one and knows no family.
/// </summary>
public sealed class AuthoredAssetTemplate
{
    /// <summary>Gets the kind's folder under a mount root, such as <c>Scripts</c>.</summary>
    public required string Folder { get; init; }

    /// <summary>Gets the stem of the proposed names, such as <c>NewScript</c>.</summary>
    public required string DefaultName { get; init; }

    /// <summary>
    /// Gets every file suffix of the kind; the first is the one created. A new name collides with a file of any of them,
    /// because both would cook to the same asset.
    /// </summary>
    public required IReadOnlyList<string> Suffixes { get; init; }

    /// <summary>Gets the function that makes a new file's text from its name, without suffix.</summary>
    public required Func<string, string> Content { get; init; }
}
