// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks the canonical forms of node paths and node references (scene instances 8.1).</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(NodeReference))]
public sealed class NodeReferenceTests
{
    private static readonly NodeId Root = NodeId.FromValue(Guid.Parse("11111111-1111-4111-8111-111111111111"));
    private static readonly NodeId Holder = NodeId.FromValue(Guid.Parse("22222222-2222-4222-8222-222222222222"));
    private static readonly NodeId Eye = NodeId.FromValue(Guid.Parse("33333333-3333-4333-8333-333333333333"));

    /// <summary>A reference to a node of the scene is the node's ID alone, as a starting camera was saved before
    /// instances; one below an instance root adds each path element.</summary>
    [TestMethod]
    public void CanonicalForm_IsTheNodeThenEachPathElement()
    {
        _ = NodeReference.To(Root).ToString().Should().Be(Root.ToString());
        _ = new NodeReference(Root, NodePath.Of(Holder, Eye)).ToString().Should().Be($"{Root}/{Holder}/{Eye}");
    }

    /// <summary>Reading the canonical form gives the same reference back.</summary>
    /// <param name="levels">The path's number of nesting levels.</param>
    [TestMethod]
    [DataRow(0)]
    [DataRow(2)]
    public void TryParse_ReadsTheCanonicalForm(int levels)
    {
        var reference = new NodeReference(Root, NodePath.Of(new[] { Holder, Eye }.Take(levels)));

        _ = NodeReference.TryParse(reference.ToString(), out var parsed).Should().BeTrue();
        _ = parsed.Should().Be(reference);
    }

    /// <summary>Text that is not a node ID followed by node IDs is not a reference.</summary>
    /// <param name="text">The text.</param>
    [TestMethod]
    [DataRow("")]
    [DataRow("not-a-guid")]
    [DataRow("00000000-0000-0000-0000-000000000000")]
    [DataRow("11111111-1111-4111-8111-111111111111/")]
    [DataRow("11111111-1111-4111-8111-111111111111//22222222-2222-4222-8222-222222222222")]
    public void TryParse_RefusesOtherText(string text)
        => _ = NodeReference.TryParse(text, out _).Should().BeFalse();

    /// <summary>Paths compare by value.</summary>
    [TestMethod]
    public void Paths_CompareByValue()
    {
        _ = NodePath.Of(Holder, Eye).Should().Be(NodePath.Of(Holder).Append(Eye));
        _ = NodePath.Of(Holder, Eye).GetHashCode().Should().Be(NodePath.Of(Holder).Append(Eye).GetHashCode());
        _ = NodePath.Of(Holder, Eye).Should().NotBe(NodePath.Of(Eye, Holder));
    }
}
