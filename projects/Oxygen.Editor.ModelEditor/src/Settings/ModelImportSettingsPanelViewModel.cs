// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;
using Oxygen.Editor.Pickers;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>The import settings panel: every setting the importer applies, for the selected files, filled from their
/// source analysis.</summary>
/// <remarks>
/// The rows bound to one file's content, the Scene and the Materials, show with one file selected; with several, the
/// panel says to select one file to choose its scene and materials. Node continuity and the selection groups need a
/// previous import and belong to the Model Editor, which sets them; the import dialog leaves them unset.
/// </remarks>
public sealed partial class ModelImportSettingsPanelViewModel : ObservableObject
{
    private const string FbxFiles = "FBX files";
    private const string GltfFiles = "glTF files";
    private const string GeneratingFiles = "Files that generate LODs";
    private const string MaterialGlyph = "";

    private IReadOnlyList<ModelImportFile> selection = [];
    private IReadOnlyList<AssetEntry> projectMaterials = [];
    private bool matchingMaterials;

    /// <summary>Initializes a new instance of the <see cref="ModelImportSettingsPanelViewModel"/> class.</summary>
    public ModelImportSettingsPanelViewModel()
    {
        this.Scene = new("Scene", ModelImportSettingCatalog.GltfScene, [], GltfFiles);
        this.UnitPolicy = new("Units", ModelImportSettingCatalog.UnitPolicy, [
            new("normalize", "Metres"), new("preserve", "File units"), new("custom", "Custom")]);
        this.UnitScale = new("Scale", ModelImportSettingCatalog.UnitScale, prefix: "×", qualifier: "Files with custom units", appliesTo: static file => IsCustomUnits(file));
        this.UpAxis = new("Up axis", ModelImportSettingCatalog.UpAxis, [
            new("from-file", "From the file"), new("y", "Y up"), new("z", "Z up")], FbxFiles);
        this.Normals = new("Normals", ModelImportSettingCatalog.NormalsPolicy, [
            new("generate", "From the file, generate missing"), new("recalculate", "Always recalculate")]);
        this.SmoothingAngle = new("Smoothing angle", ModelImportSettingCatalog.SmoothingAngle, unit: "°");
        this.Tangents = new("Tangents", ModelImportSettingCatalog.TangentsPolicy, [
            new("generate", "From the file, generate missing"), new("recalculate", "Always recalculate")]);
        this.KeepNodePivots = new("Keep node pivots", ModelImportSettingCatalog.BakeTransforms, static keep => !keep, static bake => bake is bool baked ? !baked : null);
        this.GenerateLods = new("Generate LODs", ModelImportSettingCatalog.GenerateLods);
        this.LodLevels = new("LOD levels", ModelImportSettingCatalog.LodLevels, qualifier: GeneratingFiles, appliesTo: static file => GeneratesLods(file));
        this.LodReduction = new("Reduction per level", ModelImportSettingCatalog.LodReduction, unit: "%", qualifier: GeneratingFiles, appliesTo: static file => GeneratesLods(file));
        this.DropEmptyNodes = new("Drop empty nodes", ModelImportSettingCatalog.NodePruning, static drop => drop ? "drop-empty" : "keep", static pruning => pruning is string value ? string.Equals(value, "drop-empty", StringComparison.Ordinal) : null);
        IReadOnlyList<ChoiceOption> sizes = [new(null, "Source size"), new(4096, "4096"), new(2048, "2048"), new(1024, "1024"), new(512, "512")];
        IReadOnlyList<ChoiceOption> qualities = [new("default", "Default"), new("fast", "Fast"), new("high", "High")];
        this.MaxTextureSize = new("Maximum size", ModelImportSettingCatalog.MaxTextureSize, sizes);
        this.CompressionQuality = new("Compression quality", ModelImportSettingCatalog.Bc7Quality, qualities);
        this.FlipNormalGreen = new("Flip normal map green", ModelImportSettingCatalog.FlipNormalGreen);
        this.ImportLights = new("Import lights", ModelImportSettingCatalog.ImportLights);
        this.LightIntensityScale = new("Light intensity scale", ModelImportSettingCatalog.LightIntensityScale, prefix: "×");
        this.OmittedLightRange = new("Range of lights without one", ModelImportSettingCatalog.OmittedLightRange, unit: "m", qualifier: GltfFiles);
        this.ImportCameras = new("Import cameras", ModelImportSettingCatalog.ImportCameras);
        this.SelectedTexture = new SelectedTextureGroup(sizes, qualities);
        this.SelectedTexture.Edited += (_, args) => this.SettingEdited?.Invoke(this, args);
        foreach (var row in this.Rows)
        {
            row.Edited += (_, _) =>
            {
                this.Refresh();
                this.SettingEdited?.Invoke(this, new SettingEditedEventArgs($"Edit {row.Label}"));
            };
        }
    }

    /// <summary>Occurs when a row changed the settings of the selected files, once per change a user makes.</summary>
    public event EventHandler<SettingEditedEventArgs>? SettingEdited;

    /// <summary>Gets the project's materials the Materials rows offer; its clearing choice imports the material.</summary>
    public AssetPickerModel MaterialChoices { get; } = new(
        static asset => AssetChoice.For(asset, "Material", MaterialGlyph),
        new AssetChoice("Imported", Asset: null, "Material", "The import produces it", IsEnabled: true, MaterialGlyph));

    /// <summary>Gets the glTF scene imported as the model scene.</summary>
    public ChoiceRow Scene { get; }

    /// <summary>Gets whether the import keeps the model's real size in metres, keeps the file's numbers, or applies a chosen
    /// scale.</summary>
    public ChoiceRow UnitPolicy { get; }

    /// <summary>Gets the factor of custom units.</summary>
    public NumberRow UnitScale { get; }

    /// <summary>Gets the up axis an FBX file is read in.</summary>
    public ChoiceRow UpAxis { get; }

    /// <summary>Gets how normals are made.</summary>
    public ChoiceRow Normals { get; }

    /// <summary>Gets the smoothing angle of generated normals.</summary>
    public NumberRow SmoothingAngle { get; }

    /// <summary>Gets how tangents are made.</summary>
    public ChoiceRow Tangents { get; }

    /// <summary>Gets whether node pivots are kept, the inverse of baking transforms into meshes.</summary>
    public ToggleRow KeepNodePivots { get; }

    /// <summary>Gets whether geometries with one LOD get generated LODs.</summary>
    public ToggleRow GenerateLods { get; }

    /// <summary>Gets the LODs generated after LOD 0, shown for files that generate LODs.</summary>
    public NumberRow LodLevels { get; }

    /// <summary>Gets the triangles each generated LOD removes, shown for files that generate LODs.</summary>
    public NumberRow LodReduction { get; }

    /// <summary>Gets whether empty nodes are dropped.</summary>
    public ToggleRow DropEmptyNodes { get; }

    /// <summary>Gets the largest stored texture size.</summary>
    public ChoiceRow MaxTextureSize { get; }

    /// <summary>Gets the BC7 compression quality.</summary>
    public ChoiceRow CompressionQuality { get; }

    /// <summary>Gets whether normal maps flip their green channel.</summary>
    public ToggleRow FlipNormalGreen { get; }

    /// <summary>Gets whether lights are imported.</summary>
    public ToggleRow ImportLights { get; }

    /// <summary>Gets the factor of every imported light's intensity.</summary>
    public NumberRow LightIntensityScale { get; }

    /// <summary>Gets the range of a glTF light without one.</summary>
    public NumberRow OmittedLightRange { get; }

    /// <summary>Gets whether cameras are imported.</summary>
    public ToggleRow ImportCameras { get; }

    /// <summary>Gets the Selected texture group: the settings of a texture of the selected mesh, in the Model Editor.</summary>
    public SelectedTextureGroup SelectedTexture { get; }

    /// <summary>Gets or sets the Node continuity group, which the Model Editor shows; none in the import dialog.</summary>
    [ObservableProperty]
    public partial NodeContinuityGroup? Continuity { get; set; }

    /// <summary>Gets or sets the Selected mesh group, which the Model Editor shows while a mesh is selected.</summary>
    [ObservableProperty]
    public partial SelectedMeshGroup? SelectedMesh { get; set; }

    /// <summary>Gets or sets the selected file's source materials; a selection replaces the whole list at once.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<MaterialRow> Materials { get; set; } = [];

    /// <summary>Gets or sets a value indicating whether one file is selected, so its scene and materials show.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsSelectOneNote))]
    public partial bool IsSingleSelection { get; set; }

    /// <summary>Gets or sets a value indicating whether a file is selected.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsSelectOneNote))]
    public partial bool HasSelection { get; set; }

    /// <summary>Gets a value indicating whether the panel says to select one file to choose its scene and materials.</summary>
    public bool ShowsSelectOneNote => this.HasSelection && !this.IsSingleSelection;

    /// <summary>Gets or sets a value indicating whether the Source group shows: one glTF file with several scenes.</summary>
    [ObservableProperty]
    public partial bool HasScenes { get; set; }

    /// <summary>Gets or sets a value indicating whether the Materials group shows: one file with materials.</summary>
    [ObservableProperty]
    public partial bool HasMaterials { get; set; }

    /// <summary>Gets or sets what the import does to the selected files' size and orientation with their settings, such as
    /// "Real size kept: converted from centimetres to Oxygen's metres (1 file unit = 0.01 m). Already Z up, as Oxygen is."; empty until an analysis completes.</summary>
    [ObservableProperty]
    public partial string ImportResult { get; set; } = string.Empty;

    private IEnumerable<SettingRow> Rows
        =>
        [
            this.Scene, this.UnitPolicy, this.UnitScale, this.UpAxis, this.Normals, this.SmoothingAngle, this.Tangents,
            this.KeepNodePivots, this.GenerateLods, this.LodLevels, this.LodReduction, this.DropEmptyNodes,
            this.MaxTextureSize, this.CompressionQuality, this.FlipNormalGreen, this.ImportLights,
            this.LightIntensityScale, this.OmittedLightRange, this.ImportCameras,
        ];

    /// <summary>Shows the settings of the selected files.</summary>
    /// <param name="files">The selected files, in the list's order.</param>
    public void Show(IReadOnlyList<ModelImportFile> files)
    {
        ArgumentNullException.ThrowIfNull(files);
        this.selection = [.. files];
        this.HasSelection = this.selection.Count != 0;
        this.IsSingleSelection = this.selection.Count == 1;
        this.Scene.Options = this.IsSingleSelection ? SceneOptions(this.selection[0].Facts) : [];
        this.HasScenes = this.Scene.Options.Count > 2;
        this.ShowMaterials();
        this.Refresh();
    }

    /// <summary>Offers the project's materials, as the asset feed lists them now.</summary>
    /// <param name="assets">The project's assets.</param>
    public void UpdateMaterials(IReadOnlyList<AssetEntry> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        this.projectMaterials = AssetRule.For(AssetKind.Material).Apply(assets);
        this.MaterialChoices.Update(this.projectMaterials);
        foreach (var row in this.Materials)
        {
            row.Refresh();
        }

        this.UseProjectMaterialsCommand.NotifyCanExecuteChanged();
    }

    /// <summary>Refills every row from the selected files, as an edit can change which rows apply.</summary>
    public void Refresh()
    {
        foreach (var row in this.Rows)
        {
            row.Refresh(this.selection);
        }

        // The scene row edits only the one selected file, and only when it has scenes to choose from.
        this.Scene.IsVisible = this.HasScenes;
        this.ImportResult = DescribeResult(this.selection);
        this.SelectedTexture.Refresh(this.selection);
    }

    // What the import does to the analysed files; files whose results differ say so instead of one file's.
    private static string DescribeResult(IReadOnlyList<ModelImportFile> files)
    {
        var results = files.Where(static file => file.Facts is not null)
            .Select(static file => ImportResultText.Describe(file.Facts!, file.Settings))
            .Distinct(StringComparer.Ordinal)
            .Take(2)
            .ToArray();
        return results.Length switch
        {
            0 => string.Empty,
            1 => results[0],
            _ => "Differs between the selected files; select one to see its result.",
        };
    }

    private static bool IsCustomUnits(ModelImportFile file)
        => string.Equals(ModelImportSettingCatalog.UnitPolicy.ReadFrom(file.Settings) as string, "custom", StringComparison.Ordinal);

    private static bool GeneratesLods(ModelImportFile file)
        => ModelImportSettingCatalog.GenerateLods.ReadFrom(file.Settings) as bool?
            ?? (bool)ModelImportSettingCatalog.GenerateLods.Default!;

    // The default scene first, as the file's own choice, then every scene by its selector.
    private static IReadOnlyList<ChoiceOption> SceneOptions(ModelSourceFacts? facts)
    {
        if (facts is null || facts.Scenes.IsEmpty)
        {
            return [];
        }

        var fallback = facts.Scenes.FirstOrDefault(static scene => scene.IsDefault) ?? facts.Scenes[0];
        return
        [
            new ChoiceOption(null, $"Default ({SceneName(fallback)})"),
            .. facts.Scenes.Select(static scene => new ChoiceOption(scene.Selector, SceneName(scene))),
        ];
    }

    private static string SceneName(ModelSourceScene scene)
        => scene.Name.Length != 0 ? scene.Name : "#" + scene.Index.ToString(System.Globalization.CultureInfo.InvariantCulture);

    private void ShowMaterials()
    {
        var rows = new List<MaterialRow>();
        if (this.IsSingleSelection && this.selection[0].Facts is { } facts)
        {
            foreach (var material in facts.Materials)
            {
                var row = new MaterialRow(this.selection[0], material, this.MaterialChoices);
                row.Edited += (_, _) =>
                {
                    this.UseProjectMaterialsCommand.NotifyCanExecuteChanged();
                    if (!this.matchingMaterials)
                    {
                        this.SettingEdited?.Invoke(this, new SettingEditedEventArgs($"Edit material {row.Material}"));
                    }
                };
                rows.Add(row);
            }
        }

        this.Materials = rows;

        this.HasMaterials = this.Materials.Count != 0;
        this.UseProjectMaterialsCommand.NotifyCanExecuteChanged();
    }

    private bool CanUseProjectMaterials() => this.projectMaterials.Count != 0 && this.Materials.Any(static row => row.Target is null);

    // Fills each row still Imported whose name matches exactly one project material; a name match is applied only when
    // the person asks, and each filled row can be reset.
    [RelayCommand(CanExecute = nameof(CanUseProjectMaterials))]
    private void UseProjectMaterials()
    {
        var byName = this.projectMaterials
            .GroupBy(static asset => AssetFileTypes.RemoveSuffix(asset.Identity.Name), StringComparer.Ordinal)
            .Where(static named => named.Count() == 1)
            .ToDictionary(static named => named.Key, static named => named.Single().Identity, StringComparer.Ordinal);
        // One change for the user: every row filled at once.
        var matched = this.Materials.Where(row => row.Target is null && byName.ContainsKey(row.Material)).ToList();
        if (matched.Count == 0)
        {
            return;
        }

        this.matchingMaterials = true;
        try
        {
            foreach (var row in matched)
            {
                row.Remap(byName[row.Material].VirtualPath);
            }
        }
        finally
        {
            this.matchingMaterials = false;
        }

        this.SettingEdited?.Invoke(this, new SettingEditedEventArgs("Match materials by name"));
    }
}
