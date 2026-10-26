// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The Import run flyout: the ImportTool's execution settings of the dialog's import, which an import session
/// saves with its files.</summary>
public sealed partial class ImportRunViewModel : ObservableObject
{
    private static readonly ImmutableDictionary<string, string> PipelineLabels = new Dictionary<string, string>(StringComparer.Ordinal)
    {
        ["texture"] = "Textures",
        ["buffer"] = "Buffers",
        ["material"] = "Materials",
        ["mesh_build"] = "Mesh build",
        ["geometry"] = "Geometry",
        ["scene"] = "Scene",
    }.ToImmutableDictionary(StringComparer.Ordinal);

    /// <summary>Initializes a new instance of the <see cref="ImportRunViewModel"/> class.</summary>
    /// <param name="defaults">The ImportTool's defaults, as the installed manifest schema states them.</param>
    public ImportRunViewModel(ImportRunSettings defaults)
    {
        ArgumentNullException.ThrowIfNull(defaults);
        this.Threads = new("Threads", defaults.ThreadPoolSize ?? Environment.ProcessorCount);
        this.JobsAtOnce = new("Jobs at once", defaults.MaxInFlightJobs ?? Environment.ProcessorCount);
        this.Pipelines = [.. ImportRunSettings.Pipelines.Select(name => new ImportRunPipeline(
            name,
            PipelineLabels[name],
            new("Workers", defaults.Concurrency[name].Workers!.Value),
            new("Queue", defaults.Concurrency[name].QueueCapacity!.Value)))];
        foreach (var value in this.Values)
        {
            value.PropertyChanged += this.OnValueChanged;
        }
    }

    /// <summary>Gets the worker threads of the import's CPU work pool.</summary>
    public RunValue Threads { get; }

    /// <summary>Gets the jobs the ImportTool processes at once.</summary>
    public RunValue JobsAtOnce { get; }

    /// <summary>Gets each pipeline's workers and queue capacity, in the manifest schema's order.</summary>
    public ImmutableArray<ImportRunPipeline> Pipelines { get; }

    /// <summary>Gets a value indicating whether the run sets any value, which the Import button's chevron marks.</summary>
    public bool HasCustomSettings => this.Values.Any(static value => value.IsSet);

    private IEnumerable<RunValue> Values
        => [this.Threads, this.JobsAtOnce, .. this.Pipelines.SelectMany(static pipeline => new[] { pipeline.Workers, pipeline.Queue })];

    /// <summary>Takes the values a session sets; every other value returns to its default.</summary>
    /// <param name="settings">The session's run settings.</param>
    public void Load(ImportRunSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        this.Threads.Value = settings.ThreadPoolSize;
        this.JobsAtOnce.Value = settings.MaxInFlightJobs;
        foreach (var pipeline in this.Pipelines)
        {
            var set = settings.Concurrency.GetValueOrDefault(pipeline.Name);
            pipeline.Workers.Value = set?.Workers;
            pipeline.Queue.Value = set?.QueueCapacity;
        }
    }

    /// <summary>Gets the values the run sets, in the manifest's vocabulary.</summary>
    /// <returns>The run's settings; unset values are left out.</returns>
    public ImportRunSettings ToSettings()
        => this.Pipelines.Aggregate(
            new ImportRunSettings { ThreadPoolSize = this.Threads.Value, MaxInFlightJobs = this.JobsAtOnce.Value },
            static (run, pipeline) => run.WithPipeline(pipeline.Name, new(pipeline.Workers.Value, pipeline.Queue.Value)));

    private void OnValueChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(RunValue.Value), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.HasCustomSettings));
            this.ResetAllCommand.NotifyCanExecuteChanged();
        }
    }

    [RelayCommand(CanExecute = nameof(HasCustomSettings))]
    private void ResetAll() => this.Load(ImportRunSettings.None);
}
