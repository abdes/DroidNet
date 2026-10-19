// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Controls;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>A read-only logical folder of a mount whose folders come from its assets' virtual paths: the accepted project output or a pak.</summary>
public sealed class CookedFolderTreeItemAdapter : TreeItemAdapter
{
    private readonly ILogicalFolderRoot mount;
    private readonly string nativePath;
    private readonly Dictionary<string, CookedFolderTreeItemAdapter> folders = new(StringComparer.Ordinal);

    private CookedFolderTreeItemAdapter(ILogicalFolderRoot mount, string nativePath)
        : base(isRoot: false, isHidden: false)
    {
        this.mount = mount;
        this.nativePath = nativePath;
    }

    /// <inheritdoc />
    public override string Label
    {
        get => this.nativePath[(this.nativePath.LastIndexOf('/') + 1)..];
        set => throw new InvalidOperationException("Logical folders are derived from asset paths.");
    }

    /// <summary>Gets the browser path without exposing generation storage.</summary>
    public AssetPath VirtualPath => this.mount.VirtualRootPath.Combine(this.nativePath.TrimStart('/'));

    /// <summary>Gets the same folder icon used by authored folders.</summary>
    public string IconGlyph => this.IsExpanded && this.folders.Count > 0 ? "\uE838" : "\uE8B7";

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => false;

    internal static IReadOnlyList<CookedFolderTreeItemAdapter> Build(ILogicalFolderRoot mount, IEnumerable<string> paths)
    {
        var roots = new Dictionary<string, CookedFolderTreeItemAdapter>(StringComparer.Ordinal);
        foreach (var path in paths)
        {
            var parts = path.Split('/', StringSplitOptions.RemoveEmptyEntries);
            var children = roots;
            var prefix = string.Empty;
            foreach (var part in parts.Take(Math.Max(0, parts.Length - 1)))
            {
                prefix = prefix.Length == 0 ? part : prefix + "/" + part;
                if (!children.TryGetValue(part, out var folder))
                {
                    folder = new(mount, prefix);
                    children.Add(part, folder);
                }

                children = folder.folders;
            }
        }

        return roots.Values.OrderBy(static folder => folder.Label, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => this.folders.Count;

    /// <inheritdoc />
    protected override Task LoadChildren()
    {
        foreach (var child in this.folders.Values.OrderBy(static folder => folder.Label, StringComparer.OrdinalIgnoreCase))
        {
            this.AddChildInternal(child);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc />
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (string.Equals(e.PropertyName, nameof(this.IsExpanded), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
        }
    }
}
