// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Checks the managed startup and view contract against the installed Interop definitions.</summary>
[TestClass]
public sealed class NativeSessionConversionTests
{
    /// <summary>Every exposed managed option maps to the same named native option.</summary>
    [TestMethod]
    public void ManagedOptionsPreserveNativeMeanings()
    {
        CheckEnum<PhysicsBackend, PhysicsBackendManaged>();
        CheckEnum<RendererImplementation, RendererImplementationManaged>();
        CheckEnum<ShadowQualityTier, ShadowQualityTierManaged>();
        CheckEnum<DirectionalShadowPolicy, DirectionalShadowImplementationPolicyManaged>();
        CheckEnum<FrameCaptureProvider, FrameCaptureProviderManaged>();
        CheckEnum<FrameCaptureInitMode, FrameCaptureInitModeManaged>();
        CheckEnum<CameraControlMode, CameraControlModeManaged>();
        CheckEnum<CameraViewPreset, CameraViewPresetManaged>();
    }

    /// <summary>An invalid managed enum cannot be passed through as an arbitrary native ordinal.</summary>
    [TestMethod]
    public void UnknownOptionsAreRejectedAtTheBoundary()
    {
        Action convert = () => _ = NativeSessionConversions.ToNative<CameraControlModeManaged>((CameraControlMode)99);
        _ = convert.Should().Throw<ArgumentOutOfRangeException>();
    }

    /// <summary>Omitted view options retain defaults from the native constructor.</summary>
    [TestMethod]
    public void OmittedViewOptionsRetainNativeDefaults()
    {
        var expected = new ViewConfigManaged();
        var actual = NativeSessionConversions.ToNative(new RuntimeViewConfig());
        _ = actual.Width.Should().Be(expected.Width);
        _ = actual.Height.Should().Be(expected.Height);
        _ = actual.ClearColor.Should().Be(expected.ClearColor);
        _ = actual.CompositingTarget.Should().BeNull();
    }

    /// <summary>Explicit viewport values survive native conversion exactly.</summary>
    [TestMethod]
    public void ExplicitViewOptionsSurviveNativeConversion()
    {
        var surface = Guid.NewGuid();
        var config = new RuntimeViewConfig { Name = "Editor", Purpose = "Viewport", CompositingTarget = surface, Width = 1280, Height = 720, ClearColor = new(0.1f, 0.2f, 0.3f, 0.4f) };
        var actual = NativeSessionConversions.ToNative(config);
        _ = actual.Name.Should().Be(config.Name);
        _ = actual.Purpose.Should().Be(config.Purpose);
        _ = actual.CompositingTarget.Should().Be(surface);
        _ = actual.Width.Should().Be(1280);
        _ = actual.Height.Should().Be(720);
        _ = actual.ClearColor.R.Should().Be(0.1f);
        _ = actual.ClearColor.G.Should().Be(0.2f);
        _ = actual.ClearColor.B.Should().Be(0.3f);
        _ = actual.ClearColor.A.Should().Be(0.4f);
    }

    /// <summary>Default and invalid view IDs retain their existing validity semantics.</summary>
    [TestMethod]
    public void ViewIdentityPreservesInvalidSentinels()
    {
        _ = default(RuntimeViewId).IsValid.Should().BeFalse();
        _ = RuntimeViewId.Invalid.Value.Should().Be(ViewIdManaged.Invalid.Value);
        _ = RuntimeViewId.Invalid.IsValid.Should().BeFalse();
        _ = new RuntimeViewId(42).IsValid.Should().BeTrue();
    }

    private static void CheckEnum<TManaged, TNative>()
        where TManaged : struct, Enum
        where TNative : struct, Enum
    {
        _ = Enum.GetNames<TManaged>().Should().BeEquivalentTo(Enum.GetNames<TNative>());
        foreach (var value in Enum.GetValues<TManaged>())
        {
            var native = NativeSessionConversions.ToNative<TNative>(value);
            _ = native.ToString().Should().Be(value.ToString());
            _ = Convert.ToUInt64(native, System.Globalization.CultureInfo.InvariantCulture).Should().Be(Convert.ToUInt64(value, System.Globalization.CultureInfo.InvariantCulture));
        }
    }
}
