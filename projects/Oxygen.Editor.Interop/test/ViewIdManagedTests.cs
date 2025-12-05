// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Interop;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ViewIdManaged))]
public sealed class ViewIdManagedTests
{
    [TestMethod]
    public void DefaultConstructor_IsInvalid()
    {
        var id = default(ViewIdManaged);

        _ = id.IsValid.Should().BeFalse("default-constructed ViewId should be invalid");
        _ = id.Value.Should().Be(0ul, "default value-type ViewId default value should be zero and treated as invalid");
    }

    [TestMethod]
    public void ConstructedValue_RoundTrips()
    {
        const ulong raw = 123456789ul;
        var id = new ViewIdManaged(raw);

        _ = id.IsValid.Should().BeTrue("explicitly constructed non-zero id should be valid");
        _ = id.Value.Should().Be(raw, "Value property should preserve constructor-supplied value");
    }

    [TestMethod]
    public void EqualityAndHashCode_WorkAsExpected()
    {
        var a = new ViewIdManaged(0xCAFEBABEul);
        var b = new ViewIdManaged(0xCAFEBABEul);
        var c = new ViewIdManaged(0xDEADBEEFul);

        _ = a.Equals(b).Should().BeTrue("two instances created with the same numeric value should be equal");
        _ = a.GetHashCode().Should().Be(b.GetHashCode(), "equal values must produce the same hash code");
        _ = a.Equals(c).Should().BeFalse("different numeric values must not be equal");
    }

    [TestMethod]
    public void ToString_ReturnsNonEmptyAndContainsValue()
    {
        var id = new ViewIdManaged(42ul);
        var s = id.ToString();

        _ = s.Should().NotBeNullOrEmpty("ToString should always return a usable textual representation");

        // Defensive check: ensure the string contains the numeric representation somewhere.
        _ = (s.Contains("42", StringComparison.Ordinal)
            || s.Contains("0x", StringComparison.Ordinal)
            || s.Contains("42", System.StringComparison.OrdinalIgnoreCase))
            .Should().BeTrue("ToString should include the expected numeric representation or native-to-string formatting");
    }

    [TestMethod]
    public void CanPassToManagedCallback()
    {
        var called = false;
        void Handler(ViewIdManaged vid)
        {
            called = true;
            _ = vid.Value.Should().Be(777ul, "callback should receive the supplied ViewIdManaged value");
        }

        System.Action<ViewIdManaged> cb = Handler;
        cb(new ViewIdManaged(777ul));
        _ = called.Should().BeTrue("callback was not invoked");
    }
}
