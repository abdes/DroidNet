// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using DroidNet.TestHelpers;
using AwesomeAssertions;

namespace DroidNet.Docking.Tests.Detail;
#pragma warning disable CA2000 // Dispose objects before losing scope

/// <summary>
/// Unit test cases for the <see cref="Dock.Factory" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Dock)}.{nameof(Dock.Factory)}")]
public partial class DockFactoryTests : TestSuiteWithAssertions
{
    [TestMethod]
    public void CreateDockNoArgumentConstructorReturnsInstance()
    {
        using var dock = Dock.Factory.CreateDock(typeof(CustomDock));

        _ = dock.Should()
            .NotBeNull()
            .And.BeAssignableTo<CustomDock>()
            .And.BeEquivalentTo(
                new CustomDock(),
                options => options.Excluding(exc => exc.Id));
    }

    [TestMethod]
    public void CreateDockNewDockIdAlwaysDifferent()
    {
        var ids = new List<uint>();

        for (var index = 0; index < 100; index++)
        {
            using var dock = Dock.Factory.CreateDock(typeof(CustomDock));
            _ = dock.Should().NotBeNull();
            var id = dock.Id.Value;
            _ = ids.Contains(id).Should().BeFalse();
            ids.Add(id);
        }
    }

    [TestMethod]
    public void CreateDockConstructorWithArgsReturnsInstanceUsingArgs()
    {
        const int intArg = 100;
        const string strArg = "arg-value";

        using var dock = Dock.Factory.CreateDock(typeof(CustomDock), [intArg, strArg]);

        _ = dock.Should()
            .NotBeNull()
            .And.BeAssignableTo<CustomDock>()
            .And.BeEquivalentTo(
                new CustomDock(intArg, strArg),
                options => options.Excluding(exc => exc.Id));
        _ = ((CustomDock)dock).IntArg.Should().Be(intArg);
        _ = ((CustomDock)dock).StrArg.Should().Be(strArg);
    }

    [TestMethod]
    public void CreateDockBadArgsReturnsNull()
    {
        var act = () =>
        {
            using var dock = Dock.Factory.CreateDock(typeof(CustomDock), [1, 2, 3]);
        };

        _ = act.Should().Throw<ObjectCreationException>();
    }

    [TestMethod]
    public void CreateDockInvalidTypeThrows()
    {
        var act = () =>
        {
            using var dock = Dock.Factory.CreateDock(typeof(string));
        };

        _ = act.Should().Throw<ObjectCreationException>();
#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
    }

    /// <summary>
    /// A custom dock for testing.
    /// </summary>
    private sealed partial class CustomDock : Dock
    {
        public CustomDock()
        {
        }

        public CustomDock(int intArg, string strArg)
        {
            this.IntArg = intArg;
            this.StrArg = strArg;
        }

        public string StrArg { get; } = string.Empty;

        public int IntArg { get; }
    }
}
#pragma warning restore CA2000 // Dispose objects before losing scope
