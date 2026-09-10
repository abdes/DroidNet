// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Interop.Input;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class RuntimeTransportConversionTests
{
    [TestMethod]
    public void EveryKeyAndModifier_HasAnExplicitMatchingNativeValue()
    {
        foreach (var key in Enum.GetValues<RuntimeKey>())
        {
            var expected = Enum.Parse<PlatformKey>(key.ToString());
            _ = RuntimeTransportConversion.ToNative(key).Should().Be(expected);
        }

        _ = Enum.GetValues<RuntimeKey>().Should().HaveCount(Enum.GetValues<PlatformKey>().Length);
        var invalid = () => RuntimeTransportConversion.ToNative((RuntimeKey)int.MaxValue);
        _ = invalid.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void MouseButtons_MapByMeaningIncludingExtendedButtons()
    {
        foreach (var button in Enum.GetValues<RuntimeMouseButton>())
        {
            _ = RuntimeTransportConversion.ToNative(button).Should().Be(Enum.Parse<PlatformMouseButton>(button.ToString()));
        }

        var invalid = () => RuntimeTransportConversion.ToNative((RuntimeMouseButton)int.MaxValue);
        _ = invalid.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void InputConversion_PreservesPhysicalCoordinatesMotionWheelAndTimestamp()
    {
        var timestamp = new DateTime(2026, 9, 10, 12, 30, 45, DateTimeKind.Utc);
        var position = new Vector2(1234.5f, -42.25f);
        var motion = new Vector2(-17.5f, 6.25f);
        var key = RuntimeTransportConversion.ToNative(new RuntimeKeyEvent(RuntimeKey.RightAlt, Pressed: true, Repeat: true, position, timestamp));
        var button = RuntimeTransportConversion.ToNative(new RuntimeButtonEvent(RuntimeMouseButton.ExtButton2, Pressed: false, position, timestamp));
        var move = RuntimeTransportConversion.ToNative(new RuntimeMouseMotionEvent(motion, position, timestamp));
        var wheel = RuntimeTransportConversion.ToNative(new RuntimeMouseWheelEvent(new Vector2(0, -0.5f), position, timestamp));

        _ = key.key.Should().Be(PlatformKey.RightAlt);
        _ = key.pressed.Should().BeTrue();
        _ = key.repeat.Should().BeTrue();
        _ = key.position.Should().Be(position);
        _ = key.timestamp.Should().Be(timestamp);
        _ = button.button.Should().Be(PlatformMouseButton.ExtButton2);
        _ = button.pressed.Should().BeFalse();
        _ = button.position.Should().Be(position);
        _ = button.timestamp.Should().Be(timestamp);
        _ = move.motion.Should().Be(motion);
        _ = move.position.Should().Be(position);
        _ = move.timestamp.Should().Be(timestamp);
        _ = wheel.scroll.Should().Be(new Vector2(0, -0.5f));
        _ = wheel.position.Should().Be(position);
        _ = wheel.timestamp.Should().Be(timestamp);
    }

    [TestMethod]
    public void PropertyConversion_PreservesComponentFieldAndScalarValues()
    {
        ImmutableArray<RuntimePropertyValue> values = [new(1, 23, -4.5f), new(ushort.MaxValue, 67, float.MaxValue)];
        var native = RuntimeTransportConversion.ToNative(values);

        _ = native.Should().HaveCount(2);
        for (var index = 0; index < values.Length; ++index)
        {
            _ = native[index].ComponentId.Should().Be(values[index].ComponentId);
            _ = native[index].FieldId.Should().Be(values[index].FieldId);
            _ = native[index].Value.Should().Be(values[index].Value);
        }
    }
}
