// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Oxygen.Editor.Scenes.Selection;

namespace Oxygen.Editor.Scenes.Tests.Selection;

/// <summary>A request to focus a scene property reaches the panel that edits it.</summary>
[TestClass]
public sealed class ScenePropertyFocusTests
{
    /// <summary>The request is raised once with the document, the property path and the requester.</summary>
    [TestMethod]
    public void RequestRaisesOnceWithItsValues()
    {
        var service = new SceneSelectionService();
        var document = new DocumentId(Guid.NewGuid());
        var raised = new List<ScenePropertyFocusEventArgs>();
        service.PropertyFocusRequested += (_, args) => raised.Add(args);

        service.RequestPropertyFocus(document, "/starting_camera", "Cooking");

        var request = raised.Should().ContainSingle().Subject;
        _ = request.DocumentId.Should().Be(document);
        _ = request.PropertyPath.Should().Be("/starting_camera");
        _ = request.Source.Should().Be("Cooking");
    }

    /// <summary>A request nobody listens to is dropped.</summary>
    [TestMethod]
    public void RequestWithoutSubscriberDoesNotThrow()
    {
        var service = new SceneSelectionService();

        var act = () => service.RequestPropertyFocus(new DocumentId(Guid.NewGuid()), "/starting_camera", "Cooking");

        _ = act.Should().NotThrow();
    }

    /// <summary>A missing or empty path or requester is refused.</summary>
    [TestMethod]
    public void RequestRefusesAMissingOrEmptyPathOrSource()
    {
        var service = new SceneSelectionService();
        var document = new DocumentId(Guid.NewGuid());

        _ = ((Action)(() => service.RequestPropertyFocus(document, null!, "Cooking"))).Should().Throw<ArgumentNullException>();
        _ = ((Action)(() => service.RequestPropertyFocus(document, string.Empty, "Cooking"))).Should().Throw<ArgumentException>();
        _ = ((Action)(() => service.RequestPropertyFocus(document, "/starting_camera", null!))).Should().Throw<ArgumentNullException>();
        _ = ((Action)(() => service.RequestPropertyFocus(document, "/starting_camera", string.Empty))).Should().Throw<ArgumentException>();
    }
}
