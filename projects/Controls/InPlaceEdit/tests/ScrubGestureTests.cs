// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

/// <summary>Exercises the decisions behind scrubbing a NumberBox from its label or its value.</summary>
/// <remarks>
///     WinUI cannot construct pointer, tap or wheel event arguments, so the NumberBox handlers only forward positions
///     to the gesture tested here. Wheel input has no handler at all: it scrolls the containing view.
/// </remarks>
[TestClass]
public sealed class ScrubGestureTests
{
    private static readonly double[] MovesAcrossTwoAndAHalfDips = [0.4, 0.8, 1.2, 1.6, 2.5];

    [TestMethod]
    public void DragOnALabelScrubsFromThePress()
    {
        var gesture = new ScrubGesture();

        gesture.Press(10, immediate: true);

        _ = gesture.IsScrubbing.Should().BeTrue("a label has no tap action, so its press starts the scrub");
        _ = gesture.Move(13, isInContact: true).Should().Be(new ScrubMove(Started: false, Steps: 3));
        _ = gesture.Move(11, isInContact: true).Should().Be(new ScrubMove(Started: false, Steps: -2));
    }

    [TestMethod]
    public void DragOnTheValueScrubsOnceItPassesTheThreshold()
    {
        var gesture = new ScrubGesture();

        gesture.Press(10, immediate: false);

        _ = gesture.IsScrubbing.Should().BeFalse("a press on the value may still be a click");
        _ = gesture.Move(10 + ScrubGesture.DragThreshold - 1, isInContact: true).Should().Be(default(ScrubMove));
        _ = gesture.Move(10 - ScrubGesture.DragThreshold, isInContact: true).Should().Be(new ScrubMove(Started: true, Steps: 0), "travel left counts too, and the threshold itself makes no step");
        _ = gesture.IsScrubbing.Should().BeTrue();
        _ = gesture.Move(10 - ScrubGesture.DragThreshold - 2, isInContact: true).Should().Be(new ScrubMove(Started: false, Steps: -2));
    }

    [TestMethod]
    public void ClickOnTheValueOpensTheEditor()
    {
        var gesture = new ScrubGesture();

        gesture.Press(10, immediate: false);
        _ = gesture.Move(11, isInContact: true).Should().Be(default(ScrubMove), "jitter under the threshold is still a click");

        _ = gesture.Release().Should().BeFalse();
        _ = gesture.AcceptTap().Should().BeTrue();
    }

    [TestMethod]
    public void TheTapEndingAScrubDoesNotOpenTheEditor()
    {
        var gesture = new ScrubGesture();
        gesture.Press(10, immediate: false);
        _ = gesture.Move(20, isInContact: true);

        _ = gesture.Release().Should().BeTrue();

        _ = gesture.AcceptTap().Should().BeFalse();
        _ = gesture.AcceptTap().Should().BeTrue("only the tap that ends the scrub is suppressed");
    }

    [TestMethod]
    public void AScrubWithoutAFollowingTapDoesNotSuppressTheNextClick()
    {
        var gesture = new ScrubGesture();
        gesture.Press(10, immediate: false);
        _ = gesture.Move(30, isInContact: true);
        _ = gesture.Release();

        gesture.Press(10, immediate: false);
        _ = gesture.Release();

        _ = gesture.AcceptTap().Should().BeTrue();
    }

    [TestMethod]
    public void VerticalOnlyMovementDoesNothing()
    {
        var gesture = new ScrubGesture();
        gesture.Press(10, immediate: false);

        _ = gesture.Move(10, isInContact: true).Should().Be(default(ScrubMove), "a vertical move keeps the same horizontal position");
        _ = gesture.IsScrubbing.Should().BeFalse();

        gesture.Press(10, immediate: true);
        _ = gesture.Move(16, isInContact: true);

        _ = gesture.Move(16, isInContact: true).Should().Be(new ScrubMove(Started: false, Steps: 0), "after horizontal travel, vertical moves add no steps");
    }

    [TestMethod]
    public void StepsFollowDistanceNotTheNumberOfMoves()
    {
        var gesture = new ScrubGesture();
        gesture.Press(0, immediate: true);

        var steps = MovesAcrossTwoAndAHalfDips.Sum(x => gesture.Move(x, isInContact: true).Steps);

        _ = steps.Should().Be(2, "five moves across two and a half DIPs make two whole steps");
        _ = gesture.Move(3, isInContact: true).Steps.Should().Be(1, "the fraction left over still counts");
    }

    [TestMethod]
    public void MovingBackOverAPendingValueAfterReleaseOutsideDoesNotScrub()
    {
        var gesture = new ScrubGesture();
        gesture.Press(10, immediate: false);

        _ = gesture.Move(30, isInContact: false).Should().Be(default(ScrubMove), "the button was released elsewhere");
        _ = gesture.Move(40, isInContact: true).Should().Be(default(ScrubMove));
        _ = gesture.IsScrubbing.Should().BeFalse();
    }

    [TestMethod]
    public void CancellingAScrubEndsItAndSuppressesItsTap()
    {
        var gesture = new ScrubGesture();
        gesture.Press(10, immediate: true);

        gesture.Cancel();

        _ = gesture.IsScrubbing.Should().BeFalse();
        _ = gesture.Move(30, isInContact: true).Should().Be(default(ScrubMove));
        _ = gesture.AcceptTap().Should().BeFalse();
    }
}
