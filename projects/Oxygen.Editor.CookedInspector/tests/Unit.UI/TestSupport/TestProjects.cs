// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.CookedInspector.Unit.UI.Tests.TestSupport;

/// <summary>Projects the inspection tests run against.</summary>
internal static class TestProjects
{
    /// <summary>Creates a project session with an authoring mount, and a cooked one when asked.</summary>
    /// <param name="cookedMount">Whether the project also mounts its cooked output.</param>
    /// <returns>The project session of the project.</returns>
    internal static TestSession CreateQueryProject(bool cookedMount = false)
    {
        return new(TestProject.Create(new MockFileSystem(), "C:/Query").Context with
        {
            Name = "Query",
            AuthoringMounts = cookedMount ? [new("Content", "Content"), new("Cooked", ".cooked")] : [new("Content", "Content")],
        });
    }
}
