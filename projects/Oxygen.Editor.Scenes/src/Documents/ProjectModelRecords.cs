// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>
/// The model import records of the open project: read from the project, and replaced by the content pipeline, the one
/// writer of model import records, which reimports the model in the same operation.
/// </summary>
/// <param name="session">The project session.</param>
/// <param name="pipeline">The content pipeline.</param>
/// <remarks>
/// A requested replacement is what <see cref="Read"/> answers until the pipeline has run it, so an edit undone before the
/// write happened finds the record it expects.
/// </remarks>
internal sealed class ProjectModelRecords(IProjectSession session, IContentPipelineService pipeline) : IModelImportRecordFiles
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, byte[]> pending = [];

    /// <inheritdoc/>
    public byte[]? Read(AssetPath model)
    {
        ArgumentNullException.ThrowIfNull(model);
        lock (this.gate)
        {
            if (this.pending.TryGetValue(model, out var requested))
            {
                return [.. requested];
            }
        }

        var project = session.Context;
        if (ModelRecordLookup.TryFind(project, model) is not { } record)
        {
            return null;
        }

        try
        {
            return project.FileSystem.File.ReadAllBytes(record.RecordPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// <inheritdoc/>
    public void Replace(AssetPath model, ReadOnlyMemory<byte> expected, ReadOnlyMemory<byte> replacement)
    {
        ArgumentNullException.ThrowIfNull(model);
        var requested = replacement.ToArray();
        lock (this.gate)
        {
            this.pending[model] = requested;
        }

        _ = this.ReplaceAsync(model, expected, requested, session.Context);
    }

    // The pipeline reports the outcome in the cook runs, as any reimport does; the request stops answering reads once run.
    private async Task ReplaceAsync(AssetPath model, ReadOnlyMemory<byte> expected, byte[] requested, ProjectContext project)
    {
        try
        {
            _ = await pipeline.ReplaceModelRecordAsync(model, expected, requested, project, CancellationToken.None).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // The project closed first.
        }
        finally
        {
            lock (this.gate)
            {
                if (this.pending.TryGetValue(model, out var current) && ReferenceEquals(current, requested))
                {
                    _ = this.pending.Remove(model);
                }
            }
        }
    }
}
