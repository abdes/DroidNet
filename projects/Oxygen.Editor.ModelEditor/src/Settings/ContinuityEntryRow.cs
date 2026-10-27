// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>A continuity entry: a node keeps the ID of an earlier anchor; the override bar marks an unsaved one.</summary>
public sealed partial class ContinuityEntryRow
{
    private readonly NodeContinuityGroup group;

    /// <summary>Initializes a new instance of the <see cref="ContinuityEntryRow"/> class.</summary>
    /// <param name="group">The Node continuity group.</param>
    /// <param name="entry">The entry.</param>
    /// <param name="isSaved">Whether the saved record holds the entry.</param>
    internal ContinuityEntryRow(NodeContinuityGroup group, ModelNodeContinuity entry, bool isSaved)
    {
        this.group = group;
        this.Entry = entry;
        this.IsSaved = isSaved;
    }

    /// <summary>Gets the entry.</summary>
    public ModelNodeContinuity Entry { get; }

    /// <summary>Gets the continuing node's anchor.</summary>
    public string Anchor => this.Entry.Anchor;

    /// <summary>Gets the anchor it continues.</summary>
    public string Continues => this.Entry.Continues;

    /// <summary>Gets a value indicating whether the saved record holds the entry.</summary>
    public bool IsSaved { get; }

    /// <summary>Gets the row's override bar: full until the entry is saved.</summary>
    public OverrideState OverrideState => this.IsSaved ? OverrideState.None : OverrideState.Full;

    /// <summary>Gets the remove button's tooltip.</summary>
    public string RemoveToolTip => $"Remove: {this.Anchor} gets its own ID again on Save";

    /// <summary>Removes the entry, as one step of the document's history.</summary>
    [RelayCommand]
    private void Remove() => this.group.Remove(this.Entry);

    /// <summary>Selects the continuing node in the Scene Explorer and the viewport.</summary>
    [RelayCommand]
    private void Select() => this.group.Select(this.Anchor);
}
