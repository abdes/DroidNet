// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>An anchor new in the last import, saying which vanished anchor it continues, if any.</summary>
public sealed partial class NewAnchorRow
{
    private readonly NodeContinuityGroup group;

    /// <summary>Initializes a new instance of the <see cref="NewAnchorRow"/> class.</summary>
    /// <param name="group">The Node continuity group.</param>
    /// <param name="anchor">The new anchor.</param>
    /// <param name="continues">The anchor it continues, if any.</param>
    internal NewAnchorRow(NodeContinuityGroup group, string anchor, string? continues)
    {
        this.group = group;
        this.Anchor = anchor;
        this.Continues = continues;
    }

    /// <summary>Gets the new anchor.</summary>
    public string Anchor { get; }

    /// <summary>Gets the anchor it continues, if any.</summary>
    public string? Continues { get; }

    /// <summary>Gets what the row says after the anchor, such as "continues Body/Door_FL".</summary>
    public string Note => this.Continues is { } continued ? $"continues {continued}" : string.Empty;

    /// <summary>Selects the anchor's node in the Scene Explorer and the viewport.</summary>
    [RelayCommand]
    private void Select() => this.group.Select(this.Anchor);
}
