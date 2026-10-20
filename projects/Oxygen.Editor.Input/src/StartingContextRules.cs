// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Input;

/// <summary>
/// The rules of a scene's starting input contexts, for the inspector's Play section and the cook.
/// </summary>
/// <remarks>
/// A context of the project is declared by the input source it is named under
/// (<c>/Content/Input/Player.input/OnFoot.oimap</c> by <c>/Content/Input/Player.input.json</c>); one its source does
/// not declare is gone and does not cook. A context outside the project, such as engine content, is resolved by the
/// cook in the mounted content.
/// </remarks>
public static class StartingContextRules
{
    /// <summary>The priority the input schema gives a context that declares none.</summary>
    public const int DefaultPriority = 0;

    /// <summary>Finds what the project says about a mapping context.</summary>
    /// <param name="project">The project, or <see langword="null"/> when none is open.</param>
    /// <param name="context">The mapping context's identity.</param>
    /// <param name="findContainer">Finds an input source of the project by its identity.</param>
    /// <returns>Whether the context is the project's, and the priority its input source declares.</returns>
    public static InputContextSource Describe(ProjectContext? project, AssetPath context, Func<AssetPath, InputContainer?> findContainer)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(findContainer);
        return project is null || !InputSourceFiles.IsAuthored(project, context)
            ? InputContextSource.External
            : new InputContextSource(IsProjectContext: true, DeclaredPriority(context, findContainer));
    }

    /// <summary>Finds the priority a mapping context declares.</summary>
    /// <param name="context">The mapping context's identity.</param>
    /// <param name="findContainer">Finds an input source by its identity.</param>
    /// <returns>The declared priority, or <see langword="null"/> when the source it is named under does not declare it.</returns>
    public static int? DeclaredPriority(AssetPath context, Func<AssetPath, InputContainer?> findContainer)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(findContainer);
        return AssetFileTypes.ContainerOf(context) is { } source ? findContainer(source)?.FindContext(context)?.Priority : null;
    }

    /// <summary>Gets the priority a starting input context is added with.</summary>
    /// <param name="entry">The starting input context.</param>
    /// <param name="source">What the project says about the context.</param>
    /// <returns>The authored priority, else the context's own, else <see cref="DefaultPriority"/>.</returns>
    public static int EffectivePriority(StartingInputContextData entry, InputContextSource source)
    {
        ArgumentNullException.ThrowIfNull(entry);
        ArgumentNullException.ThrowIfNull(source);
        return entry.Priority ?? source.OwnPriority;
    }

    /// <summary>Checks a starting input context the way the cook will.</summary>
    /// <param name="entry">The starting input context.</param>
    /// <param name="source">What the project says about the context.</param>
    /// <returns>The problem, with one of the <see cref="StartingContextCodes"/>, or <see langword="null"/> when the
    /// context starts.</returns>
    public static AssetProblem? Check(StartingInputContextData entry, InputContextSource source)
    {
        ArgumentNullException.ThrowIfNull(entry);
        ArgumentNullException.ThrowIfNull(source);
        var path = entry.Context.VirtualPath;
        if (!path.EndsWith(AssetFileTypes.InputMappingContext, StringComparison.Ordinal))
        {
            return new AssetProblem(
                DiagnosticSeverity.Error,
                StartingContextCodes.NotAContext,
                $"'{path}' is not an input mapping context. Remove it from the scene's starting input contexts.");
        }

        return source.IsUndeclared
            ? new AssetProblem(
                DiagnosticSeverity.Error,
                StartingContextCodes.Undeclared,
                $"No input source declares the starting input context '{path}'. Remove it, or declare it in the input source it is named under.")
            : null;
    }
}
