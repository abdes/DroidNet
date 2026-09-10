// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed partial class RuntimeDependencyTests
{
    [TestMethod]
    public void FeatureSources_CannotAcquireNativeWorldInputOrConstructNativeInputPayloads()
    {
        var repository = FindRepository();
        var violations = new List<string>();
        foreach (var project in Directory.EnumerateDirectories(Path.Combine(repository, "projects"), "Oxygen.Editor.*"))
        {
            var name = Path.GetFileName(project);
            if (name is "Oxygen.Editor.Runtime" or "Oxygen.Editor.Interop")
            {
                continue;
            }

            var source = Path.Combine(project, "src");
            if (!Directory.Exists(source))
            {
                continue;
            }

            foreach (var file in Directory.EnumerateFiles(source, "*.cs", SearchOption.AllDirectories))
            {
                var code = CommentsAndStrings().Replace(File.ReadAllText(file), string.Empty);
                if (NativeWorldOrInput().IsMatch(code))
                {
                    violations.Add(Path.GetRelativePath(repository, file));
                }
            }
        }

        _ = violations.Should().BeEmpty("feature world/input operations must use managed runtime capabilities");
    }

    [TestMethod]
    public void CapabilitySignatures_ContainNoInteropOrFeatureUiTypes()
    {
        var visited = new HashSet<Type>();
        foreach (var type in new[] { typeof(IRuntimeWorldCommands), typeof(IRuntimeInputCommands), typeof(RuntimeWorldRequest), typeof(RuntimeInputRequest) })
        {
            Inspect(type);
        }

        void Inspect(Type type)
        {
            if (!visited.Add(type))
            {
                return;
            }

            _ = (type.Namespace ?? string.Empty).Should().NotStartWith("Oxygen.Interop");
            _ = (type.Namespace ?? string.Empty).Should().NotStartWith("Microsoft.UI");
            foreach (var argument in type.GetGenericArguments())
            {
                Inspect(argument);
            }

            if (type.Assembly != typeof(IRuntimeWorldCommands).Assembly)
            {
                return;
            }

            foreach (var method in type.GetMethods())
            {
                Inspect(method.ReturnType);
                foreach (var parameter in method.GetParameters())
                {
                    Inspect(parameter.ParameterType);
                }
            }
        }
    }

    private static string FindRepository()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "design", "editor", "PLAN.md")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Editor source checkout was not found for the dependency gate.");
    }

    [GeneratedRegex("//[^\\r\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\])*\"", RegexOptions.CultureInvariant, 1000)]
    private static partial Regex CommentsAndStrings();

    [GeneratedRegex(@"\bOxygen\.Interop\.(?:World|Input)\b|\b(?:OxygenWorld|OxygenInput|Editor(?:Key|Button|MouseMotion|MouseWheel)EventManaged)\b", RegexOptions.CultureInvariant, 1000)]
    private static partial Regex NativeWorldOrInput();
}
