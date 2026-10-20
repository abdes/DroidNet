// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Input;

/// <summary>
/// The rules of input sources that need the other sources of the project. The rules of one source alone (JSON, the
/// engine's schema, one declaration per name, every action named by its name declared) are the format's, which
/// <see cref="InputSourceReader.Check"/> reports.
/// </summary>
public static class InputRules
{
    /// <summary>
    /// Checks that every action an input source names by virtual path is an action an input source of the project
    /// declares. A path into a mount the project does not author, such as a cooked library, is left to the cook, which
    /// resolves it in the mounted content.
    /// </summary>
    /// <param name="project">The project whose authoring mounts hold the input sources.</param>
    /// <param name="container">The checked source.</param>
    /// <param name="findContainer">Finds an input source of the project by its identity.</param>
    /// <returns>One <see cref="InputSourceCodes.ActionMissing"/> problem per reference that names no action.</returns>
    public static IEnumerable<AssetProblem> CheckActionPaths(ProjectContext project, InputContainer container, Func<AssetPath, InputContainer?> findContainer)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(container);
        ArgumentNullException.ThrowIfNull(findContainer);
        return container.Source.ActionReferences
            .Where(reference => reference.IsPath && !NamesAction(project, container, reference, findContainer))
            .Select(static reference => new AssetProblem(
                DiagnosticSeverity.Error,
                InputSourceCodes.ActionMissing,
                $"No input source declares the action '{reference.Text}'. Name an action as /<input source path>/<action>.oiact.")
            {
                Line = reference.Position?.Line,
                Column = reference.Position?.Column,
            });
    }

    private static bool NamesAction(ProjectContext project, InputContainer container, InputActionReference reference, Func<AssetPath, InputContainer?> findContainer)
    {
        if (!reference.TryGetAction(container.Identity, out var action) || !string.Equals(AssetFileTypes.Find(action.Name)?.CookedExtension, AssetFileTypes.InputAction, StringComparison.Ordinal))
        {
            return false;
        }

        return !InputSourceFiles.IsAuthored(project, action)
            || (AssetFileTypes.ContainerOf(action) is { } source && findContainer(source)?.FindAction(action) is not null);
    }
}
