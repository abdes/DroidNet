// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Materials;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>Keeps one asset's visual container stable while its immutable status snapshot changes.</summary>
/// <param name="item">The initial asset snapshot.</param>
public sealed partial class AssetBrowserRow(AssetEntry item) : ObservableObject
{
    private AssetEntry item = item;
    private AssetFileFacts? fileFacts;
    private MaterialPreviewColor? baseColor;
    private bool baseColorRead;
    private Scripts.ScriptAnalysisSnapshot? scriptAnalysis;

    /// <summary>Gets the current asset snapshot used by display bindings and commands.</summary>
    public AssetEntry Item => this.item;

    /// <summary>Gets the asset type used by layout commands.</summary>
    public AssetKind Kind => this.item.Kind;

    /// <summary>Gets the displayed status; a script with problems shows its error and warning counts instead.</summary>
    public string StatusText => this.scriptAnalysis is { ErrorCount: > 0 } or { WarningCount: > 0 }
        ? this.scriptAnalysis.ProblemSummary
        : this.item.PrimaryBadge;

    /// <summary>Gets the semantic tone of the displayed status: Neutral, Caution, Critical or Success.</summary>
    public string StatusTone => this.scriptAnalysis switch
    {
        { ErrorCount: > 0 } => "Critical",
        { WarningCount: > 0 } => "Caution",
        _ => AssetStatusText.GetToneForText(this.item.PrimaryBadge),
    };

    /// <summary>Gets the folder that holds the asset.</summary>
    public string Location
    {
        get
        {
            var path = this.item.DisplayPath.Replace('\\', '/');
            var slash = path.LastIndexOf('/');
            return slash <= 0 ? "/" : path[..slash];
        }
    }

    /// <summary>Gets the size and change time of the asset's file, read once per snapshot.</summary>
    public AssetFileFacts FileFacts => this.fileFacts ??= AssetFileFacts.Read(this.item);

    /// <summary>Gets the asset file size for display.</summary>
    public string SizeText => this.FileFacts.SizeText;

    /// <summary>Gets the asset file change time for display.</summary>
    public string ModifiedText => this.FileFacts.ModifiedText;

    /// <summary>Gets a material's base colour for its swatch, or null for other kinds.</summary>
    public MaterialPreviewColor? BaseColor
    {
        get
        {
            if (!this.baseColorRead)
            {
                this.baseColor = MaterialPreviewColor.TryRead(this.item);
                this.baseColorRead = true;
            }

            return this.baseColor;
        }
    }

    /// <summary>Gets a value indicating whether the row shows a colour swatch instead of its type glyph.</summary>
    public bool HasSwatch => this.BaseColor is not null;

    /// <summary>Gets a value indicating whether the row shows its type glyph.</summary>
    public bool HasGlyph => !this.HasSwatch;

    /// <summary>Shows a script's problems in the row status.</summary>
    /// <param name="analysis">The script's analysis, or <see langword="null"/> when unknown.</param>
    internal void SetScriptProblems(Scripts.ScriptAnalysisSnapshot? analysis)
    {
        if (ReferenceEquals(this.scriptAnalysis, analysis))
        {
            return;
        }

        this.scriptAnalysis = analysis;
        this.OnPropertyChanged(nameof(this.StatusText));
        this.OnPropertyChanged(nameof(this.StatusTone));
    }

    /// <summary>Updates presentation without replacing the row or its selection identity.</summary>
    /// <param name="replacement">The latest snapshot of this asset.</param>
    internal void Update(AssetEntry replacement)
    {
        if (this.item.Identity != replacement.Identity)
        {
            throw new ArgumentException("A row can only update its own asset identity.", nameof(replacement));
        }

        var previousKind = this.item.Kind;
        if (!this.SetProperty(ref this.item, replacement, nameof(this.Item)))
        {
            return;
        }

        this.fileFacts = null;
        this.baseColorRead = false;
        if (previousKind != replacement.Kind)
        {
            this.OnPropertyChanged(nameof(this.Kind));
        }

        this.OnPropertyChanged(nameof(this.StatusText));
        this.OnPropertyChanged(nameof(this.StatusTone));
        this.OnPropertyChanged(nameof(this.Location));
        this.OnPropertyChanged(nameof(this.FileFacts));
        this.OnPropertyChanged(nameof(this.SizeText));
        this.OnPropertyChanged(nameof(this.ModifiedText));
        this.OnPropertyChanged(nameof(this.BaseColor));
        this.OnPropertyChanged(nameof(this.HasSwatch));
        this.OnPropertyChanged(nameof(this.HasGlyph));
    }
}
