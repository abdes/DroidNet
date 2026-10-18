// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>A project's settings, saved in its manifest and shared by everyone who opens the project.</summary>
public sealed record ProjectSettings
{
    /// <summary>Gets a value indicating whether a changed source file is cooked without being asked.</summary>
    public bool CookOnChange { get; init; } = true;
}
