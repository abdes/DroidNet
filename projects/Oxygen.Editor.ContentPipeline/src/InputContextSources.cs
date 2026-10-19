// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Reads what a mapping context declares about itself in its input source.
/// </summary>
/// <remarks>
/// A context is named under the source that declares it (<c>/Content/Input/Player.input/OnFoot.oimap</c>), so its
/// declaration is found in that one file.
/// </remarks>
public static class InputContextSources
{
    /// <summary>The priority the input schema gives a context that declares none.</summary>
    public const int DefaultPriority = 0;

    /// <summary>Finds what the project says about a mapping context.</summary>
    /// <param name="project">The project whose authoring mounts hold the input files.</param>
    /// <param name="context">The mapping context asset.</param>
    /// <returns>Whether the context is the project's, and the priority its input file declares.</returns>
    public static InputContextSource Find(ProjectContext project, AssetPath context)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(context);
        // A context of the project is cooked from the input source it is named under; one its source does not declare
        // is gone.
        return RelocationPaths.ToPhysical(project, context) is null
            ? InputContextSource.External
            : new InputContextSource(IsProjectContext: true, FindDeclaredPriority(project, context));
    }

    /// <summary>Finds the priority a mapping context declares.</summary>
    /// <param name="project">The project whose authoring mounts hold the input files.</param>
    /// <param name="context">The mapping context asset.</param>
    /// <returns>The declared priority, or <see langword="null"/> when no input source of the project declares it.</returns>
    public static int? FindDeclaredPriority(ProjectContext project, AssetPath context)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(context);
        return context.Name.EndsWith(AssetFileTypes.InputMappingContext, StringComparison.Ordinal)
            && InputSourceFiles.FindDeclaration(project, context) is { } declaration
                ? declaration.Source.FindContext(context.Name[..^AssetFileTypes.InputMappingContext.Length])!.Priority
                : null;
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
