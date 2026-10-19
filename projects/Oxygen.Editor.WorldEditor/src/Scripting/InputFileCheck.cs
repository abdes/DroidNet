// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Scripting;

/// <summary>
/// Checks an input source the way its cook will: valid JSON, valid against the engine's input schema, one declaration
/// per name, every action named by its name declared, and every action named by its path declared by an input source
/// of the project.
/// </summary>
public static class InputFileCheck
{
    /// <summary>Checks an input source.</summary>
    /// <param name="project">The project whose input sources declare the actions named by path.</param>
    /// <param name="path">The input source file.</param>
    /// <param name="source">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <param name="bytes">The file's content.</param>
    /// <returns>The problems, empty when the source cooks.</returns>
    public static IReadOnlyList<ScriptProblem> Check(ProjectContext project, string path, AssetPath source, ReadOnlySpan<byte> bytes)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(path);
        var report = InputSourceReader.Check(bytes);
        var problems = report.Source is { } input
            ? report.Problems.Concat(InputSourceFiles.CheckActionPaths(project, source, input))
            : report.Problems;
        return [.. problems.Select(problem => ToScriptProblem(path, problem))];
    }

    private static ScriptProblem ToScriptProblem(string path, AssetProblem problem)
    {
        var location = problem.Line is { } line ? $"{path}:{line}:{problem.Column ?? 1}" : path;
        return new ScriptProblem(
            ScriptProblemSeverity.Error,
            problem.Code,
            problem.Message,
            path,
            problem.Line,
            problem.Column,
            $"error[{problem.Code}]: {problem.Message}\n --> {location}\n",
            problem.Explanation ?? "The input source does not cook until this is fixed.");
    }
}
