// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Text;

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>
/// Builds the process that opens a file in a code editor, and finds Visual Studio Code.
/// </summary>
public static class CodeEditorLaunch
{
    /// <summary>Gets the VS Code command line shown as the custom command example.</summary>
    public const string ExampleCustomCommand = "code --goto \"{file}:{line}:{column}\"";

    /// <summary>Creates the start information for opening a file.</summary>
    /// <param name="preference">The chosen editor.</param>
    /// <param name="file">The file to open.</param>
    /// <param name="line">The 1-based line, if any.</param>
    /// <param name="column">The 1-based column, if any.</param>
    /// <param name="projectRoot">The project folder, opened as the VS Code workspace so its settings apply.</param>
    /// <param name="visualStudioCode">The VS Code executable, required for <see cref="CodeEditorKind.VisualStudioCode"/>.</param>
    /// <returns>The process start information.</returns>
    /// <exception cref="InvalidOperationException">VS Code was chosen but not found, or the custom command is empty.</exception>
    public static ProcessStartInfo Create(
        CodeEditorPreference preference,
        string file,
        int? line,
        int? column,
        string? projectRoot,
        string? visualStudioCode)
    {
        ArgumentNullException.ThrowIfNull(preference);
        ArgumentException.ThrowIfNullOrWhiteSpace(file);

        var lineText = (line ?? 1).ToString(CultureInfo.InvariantCulture);
        var columnText = (column ?? 1).ToString(CultureInfo.InvariantCulture);
        switch (preference.Kind)
        {
            case CodeEditorKind.VisualStudioCode:
                var start = new ProcessStartInfo(visualStudioCode ?? throw new InvalidOperationException("Visual Studio Code was not found."))
                {
                    UseShellExecute = false,
                };
                if (!string.IsNullOrWhiteSpace(projectRoot))
                {
                    start.ArgumentList.Add(projectRoot);
                }

                start.ArgumentList.Add("--goto");
                start.ArgumentList.Add($"{file}:{lineText}:{columnText}");
                return start;

            case CodeEditorKind.SystemDefault:
                return new ProcessStartInfo(file) { UseShellExecute = true };

            default:
                var tokens = Tokenize(preference.CustomCommand ?? string.Empty);
                if (tokens.Count == 0)
                {
                    throw new InvalidOperationException("The custom code editor command is empty.");
                }

                var arguments = tokens.Skip(1).Select(Expand).ToList();
                if (!tokens.Any(static token => token.Contains("{file}", StringComparison.Ordinal)))
                {
                    arguments.Add(file);
                }

                var program = ResolveProgram(Expand(tokens[0]));
                var isBatch = Path.GetExtension(program).ToUpperInvariant() is ".CMD" or ".BAT";
                var custom = new ProcessStartInfo(isBatch ? "cmd.exe" : program) { UseShellExecute = false, CreateNoWindow = isBatch };
                if (isBatch)
                {
                    custom.ArgumentList.Add("/c");
                    custom.ArgumentList.Add(program);
                }

                foreach (var argument in arguments)
                {
                    custom.ArgumentList.Add(argument);
                }

                return custom;
        }

        string Expand(string token)
            => token.Replace("{file}", file, StringComparison.Ordinal)
                .Replace("{line}", lineText, StringComparison.Ordinal)
                .Replace("{column}", columnText, StringComparison.Ordinal)
                .Replace("{project}", projectRoot ?? string.Empty, StringComparison.Ordinal);
    }

    /// <summary>Describes a launch for messages: the program and its arguments.</summary>
    /// <param name="start">The start information.</param>
    /// <returns>The command line as the user would type it.</returns>
    public static string Describe(ProcessStartInfo start)
    {
        ArgumentNullException.ThrowIfNull(start);
        return string.Join(' ', start.ArgumentList.Prepend(start.FileName).Select(static part => part.Contains(' ', StringComparison.Ordinal) ? $"\"{part}\"" : part));
    }

    /// <summary>Finds the Visual Studio Code executable.</summary>
    /// <returns>The full path of <c>Code.exe</c>, or <see langword="null"/> when VS Code is not installed.</returns>
    public static string? FindVisualStudioCode()
    {
        var candidates = new List<string>();
        foreach (var folder in (Environment.GetEnvironmentVariable("PATH") ?? string.Empty).Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            // VS Code puts its bin folder, which holds code.cmd, on PATH; Code.exe sits one level up.
            if (File.Exists(Path.Combine(folder, "code.cmd")))
            {
                candidates.Add(Path.Combine(folder, "..", "Code.exe"));
            }
        }

        candidates.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Microsoft VS Code", "Code.exe"));
        candidates.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Microsoft VS Code", "Code.exe"));
        return candidates.Select(Path.GetFullPath).FirstOrDefault(File.Exists);
    }

    /// <summary>Resolves a program name through <c>PATH</c> and <c>PATHEXT</c>, as a command prompt would.</summary>
    /// <param name="program">The program as written in the command.</param>
    /// <returns>The full path when found; otherwise the program unchanged.</returns>
    internal static string ResolveProgram(string program)
    {
        if (Path.IsPathRooted(program) || program.Contains(Path.DirectorySeparatorChar, StringComparison.Ordinal) || program.Contains(Path.AltDirectorySeparatorChar, StringComparison.Ordinal))
        {
            return program;
        }

        string[] extensions = Path.HasExtension(program)
            ? [string.Empty]
            : (Environment.GetEnvironmentVariable("PATHEXT") ?? ".COM;.EXE;.BAT;.CMD").Split(';', StringSplitOptions.RemoveEmptyEntries);
        return (Environment.GetEnvironmentVariable("PATH") ?? string.Empty)
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .SelectMany(folder => extensions.Select(extension => Path.Combine(folder, program + extension)))
            .FirstOrDefault(File.Exists) ?? program;
    }

    /// <summary>Splits a command line into words; double quotes group a word and are removed.</summary>
    /// <param name="command">The command line.</param>
    /// <returns>The words.</returns>
    internal static List<string> Tokenize(string command)
    {
        var tokens = new List<string>();
        var current = new StringBuilder();
        var quoted = false;
        var inToken = false;
        foreach (var character in command)
        {
            if (character == '"')
            {
                quoted = !quoted;
                inToken = true;
            }
            else if (char.IsWhiteSpace(character) && !quoted)
            {
                if (inToken)
                {
                    tokens.Add(current.ToString());
                    _ = current.Clear();
                    inToken = false;
                }
            }
            else
            {
                _ = current.Append(character);
                inToken = true;
            }
        }

        if (inToken)
        {
            tokens.Add(current.ToString());
        }

        return tokens;
    }
}
