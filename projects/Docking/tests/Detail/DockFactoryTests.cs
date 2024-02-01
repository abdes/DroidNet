// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class DockFactoryTests : TestSuiteWithAssertions
{
    [TestMethod]
    public void CreateDock_NoArgumentConstructor_ReturnsInstance()
    {
        var dock = Dock.Factory.CreateDock(typeof(CustomDock));

        _ = dock.Should()
            .NotBeNull()
            .And.BeAssignableTo<CustomDock>()
            .And.BeEquivalentTo(
                new CustomDock(),
                options => options.Excluding(exc => exc.Id));
    }

    [TestMethod]
    public void CreateDock_NewDockId_AlwaysDifferent()
    {
        var ids = new List<int>();

        for (var index = 0; index < 100; index++)
        {
            var dock = Dock.Factory.CreateDock(typeof(CustomDock));
            _ = dock.Should().NotBeNull();
            var id = dock!.Id.Value;
            _ = ids.Contains(id).Should().BeFalse();
            ids.Add(id);
        }
    }

    [TestMethod]
    public void CreateDock_ConstructorWithArgs_ReturnsInstanceUsingArgs()
    {
        const int intArg = 100;
        const string strArg = "arg-value";

        var dock = Dock.Factory.CreateDock(typeof(CustomDock), [intArg, strArg]);

        _ = dock.Should()
            .NotBeNull()
            .And.BeAssignableTo<CustomDock>()
            .And.BeEquivalentTo(
                new CustomDock(intArg, strArg),
                options => options.Excluding(exc => exc.Id));
        _ = ((CustomDock)dock!).IntArg.Should().Be(intArg);
        _ = ((CustomDock)dock).StrArg.Should().Be(strArg);
    }

    [TestMethod]
    public void CreateDock_BadArgs_ReturnsNull()
    {
        var dock = Dock.Factory.CreateDock(typeof(CustomDock), [1, 2, 3]);

        _ = dock.Should().BeNull();
    }

    [TestMethod]
    public void CreateDock_InvalidType_ReturnsNull()
    {
        var dock = Dock.Factory.CreateDock(typeof(string));

#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif

        _ = dock.Should().BeNull();
    }

    private sealed class CustomDock : Dock
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
