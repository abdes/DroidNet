// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies <see cref="NodeFlagsComponent"/> defaults, change notification, persistence and the resolution of the
/// source-mode flags, as the scene model states it: Inherit copies the parent's resolved value, a local mode replaces
/// it, and a root's Inherit resolves the scene defaults Shown, On and On.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(NodeFlagsComponent))]
public sealed class NodeFlagsComponentTests
{
    [TestMethod]
    public void NewComponent_HasTheDocumentedDefaults()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };

        _ = flags.Visibility.Should().Be(VisibilitySource.Inherit);
        _ = flags.CastsShadows.Should().Be(ShadowSource.Inherit);
        _ = flags.ReceivesShadows.Should().Be(ShadowSource.Inherit);
        _ = flags.IsStatic.Should().BeFalse();
        _ = flags.IsRayCastingSelectable.Should().BeTrue();
        _ = flags.IgnoreParentTransform.Should().BeFalse();
    }

    [TestMethod]
    public void IsLocked_IsTrue()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };

        _ = flags.IsLocked.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(nameof(NodeFlagsComponent.Visibility), VisibilitySource.Hidden)]
    [DataRow(nameof(NodeFlagsComponent.CastsShadows), ShadowSource.Off)]
    [DataRow(nameof(NodeFlagsComponent.ReceivesShadows), ShadowSource.On)]
    [DataRow(nameof(NodeFlagsComponent.IsStatic), true)]
    [DataRow(nameof(NodeFlagsComponent.IsRayCastingSelectable), false)]
    [DataRow(nameof(NodeFlagsComponent.IgnoreParentTransform), true)]
    public void Setter_RaisesPropertyChangedOnceWithThePropertyName(string propertyName, object value)
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };
        var recorder = new PropertyChangeRecorder(flags);
        var property = typeof(NodeFlagsComponent).GetProperty(propertyName)!;

        property.SetValue(flags, value);

        _ = recorder.Changed.Should().Equal(propertyName);
        _ = property.GetValue(flags).Should().Be(value);
    }

    [TestMethod]
    public void DehydrateThenHydrate_RoundTripsAllSixFlags()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };
        flags.Visibility = VisibilitySource.Hidden;
        flags.CastsShadows = ShadowSource.On;
        flags.ReceivesShadows = ShadowSource.Off;
        flags.IsStatic = true;
        flags.IsRayCastingSelectable = false;
        flags.IgnoreParentTransform = true;

        var data = (NodeFlagsData)flags.Dehydrate();
        var restored = new NodeFlagsComponent { Name = "Other" };
        restored.Hydrate(data);

        _ = restored.Visibility.Should().Be(VisibilitySource.Hidden);
        _ = restored.CastsShadows.Should().Be(ShadowSource.On);
        _ = restored.ReceivesShadows.Should().Be(ShadowSource.Off);
        _ = restored.IsStatic.Should().BeTrue();
        _ = restored.IsRayCastingSelectable.Should().BeFalse();
        _ = restored.IgnoreParentTransform.Should().BeTrue();
        _ = restored.Id.Should().Be(flags.Id);
        _ = restored.Name.Should().Be(NodeFlagsComponent.DefaultName);
    }

    [TestMethod]
    public void InheritRoot_ResolvesTheSceneDefaults()
    {
        var root = new SceneNode(new Scene { Name = "Scene" }, "Root");

        _ = root.Flags.ResolveVisibility().Should().Be(new ResolvedNodeFlag(Value: true, Source: null));
        _ = root.Flags.ResolveCastsShadows().Should().Be(new ResolvedNodeFlag(Value: true, Source: null));
        _ = root.Flags.ResolveReceivesShadows().Should().Be(new ResolvedNodeFlag(Value: true, Source: null));
    }

    [TestMethod]
    [DataRow(VisibilitySource.Hidden, VisibilitySource.Shown, true, DisplayName = "Hidden parent, Shown child: Shown")]
    [DataRow(VisibilitySource.Shown, VisibilitySource.Hidden, false, DisplayName = "Shown parent, Hidden child: Hidden")]
    [DataRow(VisibilitySource.Hidden, VisibilitySource.Inherit, false, DisplayName = "Hidden parent, Inherit child: Hidden")]
    [DataRow(VisibilitySource.Shown, VisibilitySource.Inherit, true, DisplayName = "Shown parent, Inherit child: Shown")]
    public void ChildVisibility_FollowsTheParentOnlyUnderInherit(VisibilitySource parentMode, VisibilitySource childMode, bool shown)
    {
        var (parent, child) = ParentAndChild();
        parent.Flags.Visibility = parentMode;
        child.Flags.Visibility = childMode;

        _ = child.Flags.ResolveVisibility().Should().Be(new ResolvedNodeFlag(shown, childMode == VisibilitySource.Inherit ? parent : child));
    }

    [TestMethod]
    public void InheritChain_ResolvesFromTheNearestLocalAncestor()
    {
        var (parent, child) = ParentAndChild();
        var grandchild = new SceneNode(child.Scene, "Grandchild");
        grandchild.Hierarchy.SetParent(child);
        parent.Flags.CastsShadows = ShadowSource.Off;

        _ = grandchild.Flags.ResolveCastsShadows().Should().Be(new ResolvedNodeFlag(Value: false, parent));
        _ = grandchild.Flags.ResolveReceivesShadows().Should().Be(new ResolvedNodeFlag(Value: true, Source: null));
    }

    [TestMethod]
    public void Reparent_RecomputesAnInheritedFlagAndKeepsALocalOne()
    {
        var (hidden, inheriting) = ParentAndChild();
        hidden.Flags.Visibility = VisibilitySource.Hidden;
        var local = new SceneNode(hidden.Scene, "Local") { Flags = { Visibility = VisibilitySource.Shown } };
        local.Hierarchy.SetParent(hidden);
        var shown = new SceneNode(hidden.Scene, "Shown") { Flags = { Visibility = VisibilitySource.Shown } };

        inheriting.Hierarchy.SetParent(shown);
        local.Hierarchy.SetParent(shown);

        _ = inheriting.Flags.ResolveVisibility().Should().Be(new ResolvedNodeFlag(Value: true, shown));
        _ = local.Flags.Visibility.Should().Be(VisibilitySource.Shown);
        _ = local.Flags.ResolveVisibility().Should().Be(new ResolvedNodeFlag(Value: true, local));
    }

    [TestMethod]
    [DataRow(true, DisplayName = "Root")]
    [DataRow(false, DisplayName = "Child")]
    public void CreationModes_TakeTheSchemaDefaultsOfTheirPlaceInTheHierarchy(bool isRoot)
    {
        var flags = new NodeFlagsData { Name = NodeFlagsComponent.DefaultName, IsStatic = true }.WithCreationModes(isRoot);

        _ = flags.Should().BeEquivalentTo(new NodeFlagsData
        {
            Id = flags.Id,
            Name = NodeFlagsComponent.DefaultName,
            IsStatic = true,
            Visibility = isRoot ? SceneSchema.Node.RootDefaults.Visibility : SceneSchema.Node.Defaults.Visibility,
            CastsShadows = isRoot ? SceneSchema.Node.RootDefaults.CastsShadows : SceneSchema.Node.Defaults.CastsShadows,
            ReceivesShadows = isRoot ? SceneSchema.Node.RootDefaults.ReceivesShadows : SceneSchema.Node.Defaults.ReceivesShadows,
        });
    }

    [TestMethod]
    public void NewComponentAndData_TakeTheSchemaDefaults()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };
        var data = new NodeFlagsData { Name = NodeFlagsComponent.DefaultName };

        _ = flags.Visibility.Should().Be(SceneSchema.Node.Defaults.Visibility).And.Be(data.Visibility);
        _ = flags.CastsShadows.Should().Be(SceneSchema.Node.Defaults.CastsShadows).And.Be(data.CastsShadows);
        _ = flags.ReceivesShadows.Should().Be(SceneSchema.Node.Defaults.ReceivesShadows).And.Be(data.ReceivesShadows);
        _ = flags.IsStatic.Should().Be(SceneSchema.Node.Defaults.IsStatic).And.Be(data.IsStatic);
        _ = flags.IsRayCastingSelectable.Should().Be(SceneSchema.Node.Defaults.IsRayCastingSelectable).And.Be(data.IsRayCastingSelectable);
        _ = flags.IgnoreParentTransform.Should().Be(SceneSchema.Node.Defaults.IgnoreParentTransform).And.Be(data.IgnoreParentTransform);
    }

    [TestMethod]
    public void ResolvedRootFlags_TakeTheSchemaRootDefaults()
    {
        var root = new SceneNode(new Scene { Name = "Scene" }, "Root");

        _ = root.Flags.ResolveVisibility().Value.Should().Be(SceneSchema.Node.RootDefaults.Visibility == VisibilitySource.Shown);
        _ = root.Flags.ResolveCastsShadows().Value.Should().Be(SceneSchema.Node.RootDefaults.CastsShadows == ShadowSource.On);
        _ = root.Flags.ResolveReceivesShadows().Value.Should().Be(SceneSchema.Node.RootDefaults.ReceivesShadows == ShadowSource.On);
    }

    [TestMethod]
    public void UndefinedSourceModes_AreRejectedWithoutMutation()
    {
        var flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName };
        var before = flags.Dehydrate();
        Action visibility = () => flags.Visibility = (VisibilitySource)99;
        Action casts = () => flags.CastsShadows = (ShadowSource)99;
        Action receives = () => flags.ReceivesShadows = (ShadowSource)99;
        Action hydrate = () => flags.Hydrate(new NodeFlagsData { Name = "Rejected", IsStatic = true, ReceivesShadows = (ShadowSource)99 });

        _ = visibility.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be((VisibilitySource)99);
        _ = casts.Should().Throw<ArgumentOutOfRangeException>();
        _ = receives.Should().Throw<ArgumentOutOfRangeException>();
        _ = hydrate.Should().Throw<ArgumentOutOfRangeException>();
        _ = flags.Dehydrate().Should().Be(before);
    }

    private static (SceneNode Parent, SceneNode Child) ParentAndChild()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");
        child.Hierarchy.SetParent(parent);
        return (parent, child);
    }
}
