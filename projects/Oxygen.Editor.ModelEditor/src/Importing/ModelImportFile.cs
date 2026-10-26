// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Models;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>One file of the import dialog's list: its source, the model's name and folder, its settings, what its
/// analysis found and whether its name is taken.</summary>
public sealed partial class ModelImportFile : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="ModelImportFile"/> class.</summary>
    /// <param name="sourcePath">The model's primary source file.</param>
    /// <param name="name">The model's name.</param>
    /// <param name="destinationFolder">The authoring folder it goes into, as typed (<c>/Content/Vehicles</c>).</param>
    /// <param name="settings">Its import settings.</param>
    public ModelImportFile(string sourcePath, string name, string destinationFolder, JsonElement settings)
    {
        this.SourcePath = Path.GetFullPath(sourcePath);
        this.Name = name;
        this.DestinationFolder = destinationFolder;
        this.Settings = settings;
    }

    /// <summary>Gets or sets the model's primary source file; Locate points a missing file at its new place.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(FileName), nameof(SourceFolder), nameof(SourceFormat))]
    public partial string SourcePath { get; set; }

    /// <summary>Gets or sets the model's name: its folder in <see cref="DestinationFolder"/> for a source from outside
    /// the project.</summary>
    [ObservableProperty]
    public partial string Name { get; set; }

    /// <summary>Gets or sets the authoring folder the model goes into.</summary>
    [ObservableProperty]
    public partial string DestinationFolder { get; set; }

    /// <summary>Gets or sets the model's import settings, in the job vocabulary.</summary>
    [ObservableProperty]
    public partial JsonElement Settings { get; set; }

    /// <summary>Gets or sets what the analysis of the file found; null until it completes.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Facts), nameof(SourceFormat), nameof(Status), nameof(StatusText))]
    public partial ModelSourceAnalysis? Analysis { get; set; }

    /// <summary>Gets or sets a value indicating whether the file is being analyzed.</summary>
    [ObservableProperty]
    public partial bool IsAnalyzing { get; set; }

    /// <summary>Gets or sets a value indicating whether the source file is missing.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Status), nameof(StatusText))]
    public partial bool IsMissing { get; set; }

    /// <summary>Gets or sets why the model's name cannot be imported, such as a taken name; empty when it can.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Status), nameof(StatusText), nameof(HasNameProblem))]
    public partial string NameProblem { get; set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether another model holds this name and can be replaced.</summary>
    [ObservableProperty]
    public partial bool IsNameTaken { get; set; }

    /// <summary>Gets or sets a value indicating whether the file is a model already imported in place, which the import
    /// imports again with its settings.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(StatusText))]
    public partial bool IsReimport { get; set; }

    /// <summary>Gets or sets the model this import replaces, once Replace is chosen.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Status), nameof(StatusText))]
    public partial SceneImportReplacement? Replacement { get; set; }

    /// <summary>Gets the source file's name.</summary>
    public string FileName => Path.GetFileName(this.SourcePath);

    /// <summary>Gets the folder that holds the source file.</summary>
    public string SourceFolder => Path.GetDirectoryName(this.SourcePath) ?? string.Empty;

    /// <summary>Gets the analysis's facts, if it completed.</summary>
    public ModelSourceFacts? Facts => this.Analysis?.Facts;

    /// <summary>Gets the file's importer family, from its analysis or else its extension.</summary>
    public ModelSourceFormats SourceFormat
        => this.Facts?.SourceFormat
            ?? (string.Equals(Path.GetExtension(this.SourcePath), ".fbx", StringComparison.OrdinalIgnoreCase) ? ModelSourceFormats.Fbx : ModelSourceFormats.Gltf);

    /// <summary>Gets a value indicating whether the name blocks the import.</summary>
    public bool HasNameProblem => this.NameProblem.Length != 0;

    /// <summary>Gets the file's state in the list: an error blocks the import, a warning reports what the analysis found.</summary>
    public ModelImportFileStatus Status
        => this.IsMissing || this.HasNameProblem ? ModelImportFileStatus.Error
            : this.Analysis is { } analysis && (!analysis.Complete || analysis.Diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Warning))
                ? ModelImportFileStatus.Warning
                : ModelImportFileStatus.Ready;

    /// <summary>Gets the tooltip of the file's status glyph.</summary>
    public string StatusText
        => this.IsMissing ? $"'{this.FileName}' is missing. Locate it, or remove it from the list."
            : this.HasNameProblem ? this.NameProblem
            : this.Status == ModelImportFileStatus.Warning ? "The analysis found problems; see Before import."
            : this.IsReimport ? $"{this.FileName} is already imported; the import imports it again with these settings."
            : string.Empty;

    /// <summary>Gets the settings with the model's name, as its record and job hold them.</summary>
    /// <returns>The settings to import with.</returns>
    public JsonElement SettingsWithName()
        => WithName(this.Settings, this.Name.Trim());

    private static JsonElement WithName(JsonElement settings, string name)
    {
        var values = settings.ValueKind == JsonValueKind.Object
            ? settings.EnumerateObject().ToDictionary(static property => property.Name, static property => property.Value.Clone(), StringComparer.Ordinal)
            : new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        values[ModelImportSettingCatalog.NameKey] = JsonSerializer.SerializeToElement(name);
        return JsonSerializer.SerializeToElement(values);
    }
}
