// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Reads what a mapping context declares about itself in its hand-written input file.
/// </summary>
/// <remarks>
/// Input files cook their contexts into their own folder, so a context's declaring file is one of the input files
/// next to the cooked context.
/// </remarks>
public static class InputContextSources
{
    /// <summary>The priority the input schema gives a context that declares none.</summary>
    public const int DefaultPriority = 0;

    private const string ContextExtension = ".oimap";

    /// <summary>Finds what the project says about a mapping context.</summary>
    /// <param name="project">The project whose authoring mounts hold the input files.</param>
    /// <param name="context">The mapping context asset.</param>
    /// <returns>Whether the context is the project's, and the priority its input file declares.</returns>
    public static InputContextSource Find(ProjectContext project, Uri context)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(context);
        var path = Uri.UnescapeDataString(context.AbsolutePath);

        // A context of the project is cooked from an input file next to it; one that no file declares is gone.
        return RelocationPaths.ToPhysical(project, path) is null
            ? InputContextSource.External
            : new InputContextSource(IsProjectContext: true, FindDeclaredPriority(project, context));
    }

    /// <summary>Finds the priority a mapping context declares.</summary>
    /// <param name="project">The project whose authoring mounts hold the input files.</param>
    /// <param name="context">The mapping context asset.</param>
    /// <returns>The declared priority, or <see langword="null"/> when no input file in the context's folder declares
    /// it.</returns>
    public static int? FindDeclaredPriority(ProjectContext project, Uri context)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(context);
        var path = Uri.UnescapeDataString(context.AbsolutePath);
        if (!path.EndsWith(ContextExtension, StringComparison.OrdinalIgnoreCase)
            || RelocationPaths.ToPhysical(project, RelocationPaths.GetParent(path)) is not { } folder
            || !Directory.Exists(folder))
        {
            return null;
        }

        var name = Path.GetFileNameWithoutExtension(path);
        foreach (var file in Directory.EnumerateFiles(folder, "*" + InputFileReferences.InputFileSuffix).Order(StringComparer.OrdinalIgnoreCase))
        {
            string text;
            try
            {
                text = File.ReadAllText(file);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                continue;
            }

            if (InputFileReferences.FindContextPriority(text, name) is { } priority)
            {
                return priority;
            }
        }

        return null;
    }

    /// <summary>Gets the priority a starting input context is added with.</summary>
    /// <param name="project">The project whose authoring mounts hold the input files.</param>
    /// <param name="entry">The starting input context.</param>
    /// <returns>The authored priority, else the context's own, else <see cref="DefaultPriority"/>.</returns>
    public static int EffectivePriority(ProjectContext project, StartingInputContextData entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        return entry.Priority ?? FindDeclaredPriority(project, entry.Context) ?? DefaultPriority;
    }
}
