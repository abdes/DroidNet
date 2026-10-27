// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>
/// The Node continuity group of the Model Editor's import settings (model editor 5.3, scene instances SI-16): what the
/// last import reported, the anchors that vanished and the ones that are new, and every continuity entry.
/// </summary>
/// <remarks>
/// No match is guessed: a vanished anchor not yet continued offers <em>Continue…</em>, whose choice writes an entry, and
/// <em>Forget</em>, for a node deleted on purpose, which later imports no longer list; every entry can be removed. Entries are import settings: an unsaved one carries the override bar until Save, which
/// reimports. Clicking an anchor of the last import selects its node.
/// </remarks>
public sealed partial class NodeContinuityGroup : ObservableObject
{
    private readonly ModelContent model;
    private readonly Action<string> selectAnchor;

    /// <summary>Initializes a new instance of the <see cref="NodeContinuityGroup"/> class.</summary>
    /// <param name="model">The model document's content, whose record the group shows and edits.</param>
    /// <param name="selectAnchor">Selects the node of an anchor of the last import.</param>
    public NodeContinuityGroup(ModelContent model, Action<string> selectAnchor)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(selectAnchor);
        this.model = model;
        this.selectAnchor = selectAnchor;
        this.Refresh();
    }

    /// <summary>Gets what the last import reported, such as "The last import: 3 nodes vanished, 3 are new.".</summary>
    [ObservableProperty]
    public partial string Summary { get; private set; } = string.Empty;

    /// <summary>Gets the anchors the last import no longer has, each saying whether a node continues it.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasVanished))]
    public partial IReadOnlyList<VanishedAnchorRow> Vanished { get; private set; } = [];

    /// <summary>Gets the anchors new in the last import, each saying which anchor it continues, if any.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasNew))]
    public partial IReadOnlyList<NewAnchorRow> New { get; private set; } = [];

    /// <summary>Gets every continuity entry, from this and earlier imports.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasEntries), nameof(EntriesHeader))]
    public partial IReadOnlyList<ContinuityEntryRow> Entries { get; private set; } = [];

    /// <summary>Gets a value indicating whether an entry or a forgotten anchor is not saved yet, which the Save note
    /// explains.</summary>
    [ObservableProperty]
    public partial bool HasUnsaved { get; private set; }

    /// <summary>Gets a value indicating whether the last import lost anchors.</summary>
    public bool HasVanished => this.Vanished.Count != 0;

    /// <summary>Gets a value indicating whether the last import has new anchors.</summary>
    public bool HasNew => this.New.Count != 0;

    /// <summary>Gets a value indicating whether the model has continuity entries.</summary>
    public bool HasEntries => this.Entries.Count != 0;

    /// <summary>Gets the entries' header, such as "Continued nodes (2)".</summary>
    public string EntriesHeader => string.Create(CultureInfo.CurrentCulture, $"Continued nodes ({this.Entries.Count})");

    /// <summary>Shows the record as the document holds it now.</summary>
    public void Refresh()
    {
        if (this.model.Record is not { } record)
        {
            this.Summary = string.Empty;
            this.Vanished = [];
            this.New = [];
            this.Entries = [];
            this.HasUnsaved = false;
            return;
        }

        var saved = this.model.Saved?.Record.NodeContinuity ?? [];
        this.Summary = SummaryOf(record);
        this.Vanished = [.. record.VanishedNodes.Select(node => new VanishedAnchorRow(this, node.Anchor, ContinuerOf(record, node.Anchor)))];
        this.New = [.. record.NewNodes.Select(node => new NewAnchorRow(this, node.Anchor, ContinuedBy(record, node.Anchor)))];
        this.Entries = [.. record.NodeContinuity.Select(entry => new ContinuityEntryRow(this, entry, isSaved: saved.Contains(entry)))];
        var forgotten = (this.model.Saved?.Record.VanishedNodes ?? []).Any(node => !record.VanishedNodes.Contains(node));
        this.HasUnsaved = forgotten || this.Entries.Any(static entry => !entry.IsSaved);
    }

    /// <summary>Gets the nodes a vanished anchor can be continued by: the nodes new in the last import first, then every
    /// node of the last import.</summary>
    /// <param name="vanished">The vanished anchor.</param>
    /// <returns>The choices.</returns>
    internal IReadOnlyList<ContinueTarget> TargetsFor(string vanished)
    {
        if (this.model.Record is not { } record)
        {
            return [];
        }

        var created = record.NewNodes.Select(static node => node.Anchor).ToHashSet(StringComparer.Ordinal);
        return
        [
            .. record.NewNodes.Select(node => TargetOf(record, node.Anchor, isNew: true, vanished)),
            .. record.PublishedNodes.Where(node => !created.Contains(node.Anchor)).Select(node => TargetOf(record, node.Anchor, isNew: false, vanished)),
        ];
    }

    /// <summary>Makes a node continue a vanished anchor, as one step of the document's history.</summary>
    /// <param name="target">The continuing node.</param>
    /// <param name="vanished">The vanished anchor.</param>
    internal void Continue(ContinueTarget target, string vanished) => this.model.AddContinuity(target.Anchor, vanished);

    /// <summary>Forgets a vanished anchor that no node continues, as one step of the document's history.</summary>
    /// <param name="anchor">The vanished anchor.</param>
    internal void Forget(string anchor) => this.model.ForgetVanished(anchor);

    /// <summary>Removes a continuity entry, as one step of the document's history.</summary>
    /// <param name="entry">The entry.</param>
    internal void Remove(ModelNodeContinuity entry) => this.model.RemoveContinuity(entry.Anchor);

    /// <summary>Selects the node of an anchor of the last import.</summary>
    /// <param name="anchor">The anchor.</param>
    internal void Select(string anchor) => this.selectAnchor(anchor);

    private static string SummaryOf(ModelImportSettings record)
    {
        var vanished = record.VanishedNodes.Length;
        var created = record.NewNodes.Length;
        return vanished == 0 && created == 0
            ? "The last import kept every node."
            : string.Create(CultureInfo.CurrentCulture, $"The last import: {Nodes(vanished)} vanished, {created} {(created == 1 ? "is" : "are")} new. No match is guessed.");

        static string Nodes(int count) => string.Create(CultureInfo.CurrentCulture, $"{count} {(count == 1 ? "node" : "nodes")}");
    }

    // The anchor of the node that continues a vanished anchor, if an entry says one does.
    private static string? ContinuerOf(ModelImportSettings record, string vanished)
        => record.NodeContinuity.FirstOrDefault(entry => string.Equals(entry.Continues, vanished, StringComparison.Ordinal))?.Anchor;

    // The anchor a node continues, if an entry says it does.
    private static string? ContinuedBy(ModelImportSettings record, string anchor)
        => record.NodeContinuity.FirstOrDefault(entry => string.Equals(entry.Anchor, anchor, StringComparison.Ordinal))?.Continues;

    private static ContinueTarget TargetOf(ModelImportSettings record, string anchor, bool isNew, string vanished)
        => new(anchor, isNew, ContinuedBy(record, anchor), vanished);
}
