// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Interop;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ViewConfigManaged))]
public sealed class ViewConfigManagedTests
{
    [TestMethod]
    public void DefaultConstructor_InitializesDefaults()
    {
        var cfg = new ViewConfigManaged();

        _ = cfg.Name.Should().NotBeNull("Name should be initialized to a string (possibly empty)");
        _ = cfg.Purpose.Should().NotBeNull("Purpose should be initialized to a string (possibly empty)");
        _ = cfg.Width.Should().Be(1u, "Default width must be the safe fallback value");
        _ = cfg.Height.Should().Be(1u, "Default height must be the safe fallback value");
        _ = cfg.CompositingTarget.HasValue.Should().BeFalse("Default should not specify a compositing target GUID");
        _ = cfg.ClearColor.A.Should().Be(1.0f, "Default clear color alpha should be 1.0f");
    }

    [TestMethod]
    public void Properties_CanBeSetAndRead()
    {
        var cfg = new ViewConfigManaged();
        cfg.Name = "Main";
        cfg.Purpose = "Preview";
        cfg.Width = 800u;
        cfg.Height = 600u;
        cfg.ClearColor = new ColorManaged(0.25f, 0.5f, 0.75f, 0.9f);
        var g = System.Guid.NewGuid();
        cfg.CompositingTarget = g;

        _ = cfg.Name.Should().Be("Main");
        _ = cfg.Purpose.Should().Be("Preview");
        _ = cfg.Width.Should().Be(800u);
        _ = cfg.Height.Should().Be(600u);
        _ = cfg.ClearColor.R.Should().Be(0.25f);
        _ = cfg.ClearColor.G.Should().Be(0.5f);
        _ = cfg.ClearColor.B.Should().Be(0.75f);
        _ = cfg.ClearColor.A.Should().Be(0.9f);
        _ = cfg.CompositingTarget.HasValue.Should().BeTrue();
        _ = cfg.CompositingTarget.Value.Should().Be(g);
    }
}
