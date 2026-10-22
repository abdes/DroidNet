// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Rendering;

/// <summary>
/// One node flag with a source mode in the Rendering section: the selected mode's index and the caption line, which
/// gives the value Inherit resolves to and where it comes from, counts mixed modes, and adds the section's notice.
/// </summary>
/// <remarks>Mode index 0 is Inherit, 1 the local On value and 2 the local Off value, as the source-mode enums number them.</remarks>
public sealed partial class NodeFlagSourceField : ObservableObject
{
    private const int InheritIndex = 0;

    private readonly Func<int> readIndex;
    private readonly Action<int> writeIndex;
    private readonly Func<NodeFlagsComponent, int> modeOf;
    private readonly Func<NodeFlagsComponent, ResolvedNodeFlag> resolve;
    private readonly Action<NodeId>? selectNode;
    private SceneNode? source;

    /// <summary>Initializes a new instance of the <see cref="NodeFlagSourceField"/> class.</summary>
    /// <param name="modeNames">The mode names, Inherit then the local On and Off values.</param>
    /// <param name="readIndex">Reads the bound mode's index, -1 when the selection is mixed.</param>
    /// <param name="writeIndex">Requests a mode for the selection.</param>
    /// <param name="modeOf">Reads one node's mode index.</param>
    /// <param name="resolve">Resolves one node's value.</param>
    /// <param name="selectNode">Selects the node a resolved value comes from, when the host can.</param>
    internal NodeFlagSourceField(
        IReadOnlyList<string> modeNames,
        Func<int> readIndex,
        Action<int> writeIndex,
        Func<NodeFlagsComponent, int> modeOf,
        Func<NodeFlagsComponent, ResolvedNodeFlag> resolve,
        Action<NodeId>? selectNode)
    {
        this.ModeNames = modeNames;
        this.readIndex = readIndex;
        this.writeIndex = writeIndex;
        this.modeOf = modeOf;
        this.resolve = resolve;
        this.selectNode = selectNode;
    }

    /// <summary>Gets the mode names, in index order.</summary>
    public IReadOnlyList<string> ModeNames { get; }

    /// <summary>Gets or sets the selected mode's index; -1 when the selected nodes' modes differ.</summary>
    public int SelectedIndex
    {
        get => this.readIndex();
        set
        {
            // A mixed refresh deselects every mode; that is not a request.
            if (value >= 0)
            {
                this.writeIndex(value);
            }
        }
    }

    /// <summary>Gets the caption line.</summary>
    public string Caption { get; private set; } = string.Empty;

    /// <summary>Gets the tooltip of the button that selects the node the value comes from; empty when there is none.</summary>
    public string SourceToolTip { get; private set; } = string.Empty;

    /// <summary>Selects the node the resolved value comes from.</summary>
    public void SelectSource()
    {
        if (this.source is { } node)
        {
            this.selectNode?.Invoke(node.Id);
        }
    }

    /// <summary>Recomputes the caption for the selected nodes.</summary>
    /// <param name="nodes">The selected nodes.</param>
    /// <param name="notice">A consequence notice the section adds, or <see langword="null"/>.</param>
    internal void Refresh(IReadOnlyCollection<SceneNode> nodes, string? notice)
    {
        this.source = null;
        var modes = nodes.Select(node => this.modeOf(node.Flags)).ToList();
        var caption = modes.Distinct().Count() switch
        {
            0 => string.Empty,
            1 when modes[0] == InheritIndex => this.DescribeInherited(nodes),
            1 => string.Empty,
            _ => "Mixed: " + Count(modes, this.ModeNames),
        };

        this.Caption = string.Join(" ", new[] { caption, notice ?? string.Empty }.Where(static part => part.Length > 0));
        this.SourceToolTip = this.source is { } node && this.selectNode is not null ? $"Select {node.Metadata.Name}" : string.Empty;
        this.OnPropertyChanged(nameof(this.SelectedIndex));
        this.OnPropertyChanged(nameof(this.Caption));
        this.OnPropertyChanged(nameof(this.SourceToolTip));
    }

    private static string Count(IEnumerable<int> indices, IReadOnlyList<string> names)
        => string.Join(
            ", ",
            indices.GroupBy(static index => index).OrderBy(static group => group.Key)
                .Select(group => string.Create(CultureInfo.CurrentCulture, $"{group.Count()} {names[group.Key]}")));

    private string DescribeInherited(IReadOnlyCollection<SceneNode> nodes)
    {
        var resolved = nodes.Select(node => this.resolve(node.Flags)).ToList();
        var values = resolved.Select(static flag => flag.Value).Distinct().ToList();
        if (values.Count > 1)
        {
            return "Resolves to " + Count(resolved.Select(static flag => flag.Value ? 1 : 2), this.ModeNames);
        }

        var value = this.ModeNames[values[0] ? 1 : 2];
        var sources = resolved.Select(static flag => flag.Source).Distinct().ToList();
        if (sources.Count > 1)
        {
            return $"{value}, from several ancestors";
        }

        this.source = sources[0];
        return this.source is { } ancestor ? $"{value}, from {ancestor.Metadata.Name}" : $"{value}, the scene default";
    }
}
