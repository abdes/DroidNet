// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>
/// The stored, per-user code editor choice.
/// </summary>
/// <param name="Version">The payload version.</param>
/// <param name="Kind">The chosen editor.</param>
/// <param name="CustomCommand">The command line of a <see cref="CodeEditorKind.Custom"/> editor.</param>
public sealed record CodeEditorPreference(int Version, CodeEditorKind Kind, string? CustomCommand)
{
    /// <summary>The payload version this editor reads and writes.</summary>
    public const int CurrentVersion = 1;

    /// <summary>Gets a value indicating whether the preference can be used as stored.</summary>
    public bool IsValid => this.Version == CurrentVersion && Enum.IsDefined(this.Kind)
        && (this.Kind != CodeEditorKind.Custom || !string.IsNullOrWhiteSpace(this.CustomCommand));
}
