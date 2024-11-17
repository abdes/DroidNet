// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
using FluentAssertions;

namespace DroidNet.Docking.Tests;
#pragma warning disable CA2000 // Dispose objects before losing scope

/// <summary>
/// Unit test cases for the <see cref="Dockable.Factory" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Dockable)}.{nameof(Dockable.Factory)}")]
public partial class DockableFactoryTests : TestSuiteWithAssertions
{
    private const string DockableId1 = "dockable1";
    private const string DockableId2 = "dockable2";

    [TestMethod]
    public void CreateDockable_GivenNewId_ShouldCreateAndManageDockable()
    {
        // Act
        using var dockable = Dockable.Factory.CreateDockable(DockableId1);

        // Assert
        _ = dockable.Should().NotBeNull("we provided a new id");
        _ = Dockable.Factory.TryGetDockable(DockableId1, out var managedDockable)
            .Should()
            .BeTrue("the created dockable should be managed");
        _ = managedDockable.Should().Be(dockable, "it is the dockable we created");
    }

    [TestMethod]
    public void CreateDockable_BadArgs_ReturnsNull()
    {
        var act = () =>
        {
            using var dockable = Dockable.Factory.CreateDockable("id", [1, 2, 3]);
        };

        _ = act.Should().Throw<ObjectCreationException>();
    }

    [TestMethod]
    public void CreateDockable_InvalidType_Throws()
    {
        var act = () =>
        {
            using var dockable = Dockable.Factory.CreateDockable(typeof(string), "id");
        };

        _ = act.Should().Throw<ObjectCreationException>();
#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
    }

    [TestMethod]
    public void CreateDockable_GivenUsedId_ShouldThrowInvalidOperationException()
    {
        // Arrange
        using var dockable = Dockable.Factory.CreateDockable(DockableId1);

        // Act
        var act = () =>
        {
            using var bad = Dockable.Factory.CreateDockable(DockableId1);
        };

        // Assert
        _ = act.Should()
            .Throw<InvalidOperationException>()
            .WithMessage($"attempt to create a dockable with an already used ID: {DockableId1}");
    }

    [TestMethod]
    public void TryGetDockable_GivenExistingId_ShouldReturnDockable()
    {
        // Arrange
        using var dockable = Dockable.Factory.CreateDockable(DockableId1);

        // Act
        var isDockableFound = Dockable.Factory.TryGetDockable(DockableId1, out var foundDockable);

        // Assert
        _ = isDockableFound.Should().BeTrue("the dockable with the given id exists");
        _ = foundDockable.Should().Be(dockable, "it is the dockable with the given id");
    }

    [TestMethod]
    public void TryGetDockable_GivenNonExistingId_ShouldNotReturnDockable()
    {
        // Arrange
        const string nonExistingId = "nonExistingId";

        // Act
        var isDockableFound = Dockable.Factory.TryGetDockable(nonExistingId, out var foundDockable);

        // Assert
        _ = isDockableFound.Should().BeFalse("there is no dockable with the given id");
        _ = foundDockable.Should().BeNull("there is no dockable with the given id");
    }

    [TestMethod]
    public void ReleaseDockable_GivenExistingId_ShouldRemoveDockable()
    {
        // Arrange
        using var dockable1 = Dockable.Factory.CreateDockable(DockableId1);

        // Act
        Dockable.Factory.ReleaseDockable(DockableId1);

        // Assert
        _ = Dockable.Factory.TryGetDockable(DockableId1, out _)
            .Should()
            .BeFalse("the dockable should be removed");
    }

    [TestMethod]
    public void DockablesEnumerator_MoveNextAndCurrent_GivenDockables_ShouldEnumerateDockables()
    {
        // Arrange
        using var dockable1 = Dockable.Factory.CreateDockable(DockableId1);
        using var dockable2 = Dockable.Factory.CreateDockable(DockableId2);
        _ = dockable1.Should().NotBeNull();
        _ = dockable2.Should().NotBeNull();

        var enumerator = new Dockable.Factory.DockablesEnumerator();

        // Act and Assert
        var dockables = new List<Dockable>();
        while (enumerator.MoveNext())
        {
            dockables.Add((Dockable)enumerator.Current);
        }

        // Assert
        _ = dockables.Should().Contain(dockable1, "it was added to the dockables");
        _ = dockables.Should().Contain(dockable2, "it was added to the dockables");
    }

    [TestMethod]
    public void CreateCustomDockable_NoArgumentConstructor_ReturnsInstance()
    {
        using var dockable = Dockable.Factory.CreateDockable(typeof(CustomDockable), DockableId1);

        _ = dockable.Should()
            .NotBeNull()
            .And.BeAssignableTo<CustomDockable>()
            .And.BeEquivalentTo(
                new CustomDockable(DockableId1),
                options => options.Excluding(exc => exc.Id));
    }

    [TestMethod]
    public void CreateCustomDockable_ConstructorWithArgs_ReturnsInstanceUsingArgs()
    {
        const int intArg = 100;
        const string strArg = "arg-value";

        using var dockable = Dockable.Factory.CreateDockable(typeof(CustomDockable), DockableId1, [intArg, strArg]);

        _ = dockable.Should()
            .NotBeNull()
            .And.BeAssignableTo<CustomDockable>()
            .And.BeEquivalentTo(
                new CustomDockable(DockableId1, intArg, strArg),
                options => options.Excluding(exc => exc.Id));
        _ = ((CustomDockable)dockable).IntArg.Should().Be(intArg);
        _ = ((CustomDockable)dockable).StrArg.Should().Be(strArg);
    }

    /// <summary>
    /// A custom implementation of <see cref="Dockable" /> for testing.
    /// </summary>
    private sealed partial class CustomDockable : Dockable
    {
        public CustomDockable(string id)
            : base(id)
        {
        }

        public CustomDockable(string id, int intArg, string strArg)
            : base(id)
        {
            this.IntArg = intArg;
            this.StrArg = strArg;
        }

        public string StrArg { get; } = string.Empty;

        public int IntArg { get; }
    }
}
#pragma warning restore CA2000 // Dispose objects before losing scope
