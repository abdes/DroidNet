// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>
/// An anchor the last import no longer has: whether a node continues it, and, when none does, <em>Continue…</em>, whose
/// flyout lists the nodes new in the last import first, then every node, with Find.
/// </summary>
public sealed partial class VanishedAnchorRow : ObservableObject
{
    private readonly NodeContinuityGroup group;
    private IReadOnlyList<ContinueTarget> targets = [];

    /// <summary>Initializes a new instance of the <see cref="VanishedAnchorRow"/> class.</summary>
    /// <param name="group">The Node continuity group.</param>
    /// <param name="anchor">The vanished anchor.</param>
    /// <param name="continuedBy">The anchor of the node that continues it, if any.</param>
    internal VanishedAnchorRow(NodeContinuityGroup group, string anchor, string? continuedBy)
    {
        this.group = group;
        this.Anchor = anchor;
        this.ContinuedBy = continuedBy;
    }

    /// <summary>Gets the vanished anchor.</summary>
    public string Anchor { get; }

    /// <summary>Gets the anchor of the node that continues it, if any.</summary>
    public string? ContinuedBy { get; }

    /// <summary>Gets a value indicating whether no node continues it, so <em>Continue…</em> shows.</summary>
    public bool CanContinue => this.ContinuedBy is null;

    /// <summary>Gets what the row says, such as "continued by Body/Door_Front_L" or "not continued".</summary>
    public string Status => this.ContinuedBy is { } node ? $"continued by {node}" : "not continued";

    /// <summary>Gets the flyout's title.</summary>
    public string FlyoutTitle => $"Continue {this.Anchor} in";

    /// <summary>Gets or sets the text the flyout's choices are filtered by.</summary>
    [ObservableProperty]
    public partial string FindText { get; set; } = string.Empty;

    /// <summary>Gets the nodes that match <see cref="FindText"/>: the ones new in the last import first, then the others.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<ContinueTarget> Targets { get; private set; } = [];

    /// <summary>Gets or sets the node chosen to continue the anchor.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Caution))]
    [NotifyCanExecuteChangedFor(nameof(ContinueCommand))]
    public partial ContinueTarget? Chosen { get; set; }

    /// <summary>Gets what continuing with the chosen node does to the changes scenes made.</summary>
    public string Caution => this.Chosen?.Caution ?? string.Empty;

    /// <summary>Fills the flyout with the nodes of the last import, as it opens.</summary>
    public void Prepare()
    {
        this.targets = this.group.TargetsFor(this.Anchor);
        this.Chosen = null;
        this.FindText = string.Empty;
        this.Filter();
    }

    partial void OnFindTextChanged(string value) => this.Filter();

    // The list keeps its order; Find matches any part of an anchor, ignoring case.
    private void Filter()
    {
        var found = this.targets.Where(target => target.Anchor.Contains(this.FindText, StringComparison.OrdinalIgnoreCase)).ToList();
        this.Targets = found;
        if (this.Chosen is { } chosen && !found.Contains(chosen))
        {
            this.Chosen = null;
        }
    }

    private bool CanContinueWithChosen() => this.Chosen is { IsEnabled: true };

    /// <summary>Makes the chosen node continue the anchor, as one step of the document's history.</summary>
    [RelayCommand(CanExecute = nameof(CanContinueWithChosen))]
    private void Continue()
    {
        if (this.Chosen is { } chosen)
        {
            this.group.Continue(chosen, this.Anchor);
        }
    }
}
