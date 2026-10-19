// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Finds the input source that declares an action or a mapping context of the project.
/// </summary>
/// <remarks>
/// A product is named under its source (<c>/Content/Input/Player.input/Jump.oiact</c> is declared by
/// <c>/Content/Input/Player.input.json</c>), so its source follows from its identity; nothing scans a folder.
/// </remarks>
public static class InputSourceFiles
{
    /// <summary>Finds the project file that declares a product.</summary>
    /// <param name="project">The project whose authoring mounts hold the input sources.</param>
    /// <param name="product">The action's or mapping context's identity.</param>
    /// <returns>The source file, or <see langword="null"/> when the product is not named under an input source the
    /// project holds.</returns>
    public static FileSystemPath? FindSourceFile(ProjectContext project, AssetPath product)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(product);
        return AssetFileTypes.ContainerOf(product) is { } container
            && RelocationPaths.ToPhysical(project, container.WithAuthoringSuffix()) is { } file
            && File.Exists(file.FullPath)
                ? file
                : null;
    }

    /// <summary>Finds the declaration of a product in its source.</summary>
    /// <param name="project">The project whose authoring mounts hold the input sources.</param>
    /// <param name="product">The action's or mapping context's identity.</param>
    /// <returns>The declaration, or <see langword="null"/> when no readable source of the project declares it.</returns>
    public static InputProductDeclaration? FindDeclaration(ProjectContext project, AssetPath product)
    {
        if (FindSourceFile(project, product) is not { } file || Read(file) is not { } source)
        {
            return null;
        }

        var extension = AssetFileTypes.Find(product.Name)!.CookedExtension!;
        var name = product.Name[..^extension.Length];
        return source.FindDeclaration(name, extension) is { } position ? new InputProductDeclaration(file, source, position) : null;
    }

    /// <summary>
    /// Checks that every action an input source names by virtual path is an action an input source of the project
    /// declares. A path into a mount the project does not author, such as a cooked library, is left to the cook, which
    /// resolves it in the mounted content.
    /// </summary>
    /// <param name="project">The project whose authoring mounts hold the input sources.</param>
    /// <param name="source">The checked source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <param name="input">The checked source.</param>
    /// <returns>One <see cref="InputSourceCodes.ActionMissing"/> problem per reference that names no action.</returns>
    public static IEnumerable<AssetProblem> CheckActionPaths(ProjectContext project, AssetPath source, InputSource input)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(input);
        foreach (var reference in input.ActionReferences.Where(static reference => reference.IsPath))
        {
            var named = reference.TryGetAction(source, out var action)
                && AssetFileTypes.Find(action.Name)?.CookedExtension == AssetFileTypes.InputAction;
            if (named && RelocationPaths.FindWritableMount(project, action!) is null)
            {
                continue;
            }

            if (!named || FindDeclaration(project, action!) is null)
            {
                yield return new AssetProblem(
                    DiagnosticSeverity.Error,
                    InputSourceCodes.ActionMissing,
                    $"No input source declares the action '{reference.Text}'. Name an action as /<input source path>/<action>.oiact.")
                {
                    Line = reference.Position?.Line,
                    Column = reference.Position?.Column,
                };
            }
        }
    }

    /// <summary>Reads an input source the way the cooker will.</summary>
    /// <param name="file">The source file.</param>
    /// <returns>The source, or <see langword="null"/> when it cannot be read or does not match the engine schema.</returns>
    public static InputSource? Read(FileSystemPath file)
    {
        ArgumentNullException.ThrowIfNull(file);
        try
        {
            return InputSourceReader.Check(File.ReadAllBytes(file.FullPath)).Source;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
