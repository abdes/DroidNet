// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Config.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Result")]
public class ResultTests
{
    [TestMethod]
    public void Ok_WithValue_IsSuccess()
    {
        var r = Result.Ok(42);

        _ = r.IsSuccess.Should().BeTrue();
        _ = r.IsFailure.Should().BeFalse();
        _ = r.Value.Should().Be(42);
    }

    [TestMethod]
    public void Fail_WithException_IsFailureAndErrorMatches()
    {
        var ex = new InvalidOperationException("boom");
        var r = Result.Fail<int>(ex);

        _ = r.IsFailure.Should().BeTrue();
        _ = r.IsSuccess.Should().BeFalse();
        _ = r.Error.Should().BeSameAs(ex);
    }

    [TestMethod]
    public void Value_Get_OnFailure_ThrowsInvalidOperationException()
    {
        var r = Result.Fail<int>(new NotSupportedException("err"));
        Action act = () => _ = r.Value;
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("No value present for a failed result.*");
    }

    [TestMethod]
    public void Error_Get_OnSuccess_ThrowsInvalidOperationException()
    {
        var r = Result.Ok(1);
        Action act = () => _ = r.Error;
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("No error present for a successful result.*");
    }

    [TestMethod]
    public void Equals_SuccessValues_AreEqual()
    {
        var a = Result.Ok("hello");
        var b = Result.Ok("hello");

        _ = (a == b).Should().BeTrue();
        _ = a.Equals(b).Should().BeTrue();
        _ = a.GetHashCode().Should().Be(b.GetHashCode());
    }

    [TestMethod]
    public void Equals_Failures_CompareByExceptionIdentity()
    {
        var ex = new NotSupportedException("x");
        var a = Result.Fail<string>(ex);
        var b = Result.Fail<string>(ex);
        var c = Result.Fail<string>(new NotSupportedException("x"));

        _ = a.Equals(b).Should().BeTrue();
        _ = (a == b).Should().BeTrue();

        // different exception instance => not equal
        _ = a.Equals(c).Should().BeFalse();
        _ = (a == c).Should().BeFalse();
    }

    [TestMethod]
    public void Ok_NullReferenceValue_HandledAndEqual()
    {
        var a = Result.Ok<string?>(value: null);
        var b = Result.Ok<string?>(value: null);

        _ = a.IsSuccess.Should().BeTrue();
        _ = a.Value.Should().BeNull();
        _ = a.Equals(b).Should().BeTrue();
    }

    [TestMethod]
    public void GetHashCode_SuccessWithNullValue_DoesNotThrow()
    {
        var r = Result.Ok<string?>(value: null);
        Action act = () => _ = r.GetHashCode();
        _ = act.Should().NotThrow(); // just ensure it doesn't throw
    }

    [TestMethod]
    public void Map_Success_TransformsValue_And_Failure_PreservesError()
    {
        var ok = Result.Ok(5);
        var mapped = ok.Map(x => x * 2);
        _ = mapped.IsSuccess.Should().BeTrue();
        _ = mapped.Value.Should().Be(10);

        var ex = new InvalidOperationException("err");
        var fail = Result.Fail<int>(ex);
        var mappedFail = fail.Map(x => x * 2);
        _ = mappedFail.IsFailure.Should().BeTrue();
        _ = mappedFail.Error.Should().BeSameAs(ex);
    }

    [TestMethod]
    public void Bind_Success_Chains_And_Failure_PreservesError()
    {
        static Result<int> Div(int a, int b) => b == 0 ? Result.Fail<int>(new DivideByZeroException()) : Result.Ok(a / b);

        var res = Result.Ok(10).Bind(x => Div(x, 2));
        _ = res.IsSuccess.Should().BeTrue();
        _ = res.Value.Should().Be(5);

        var fail = Result.Ok(10).Bind(x => Div(x, 0));
        _ = fail.IsFailure.Should().BeTrue();
        _ = fail.Error.Should().BeOfType<DivideByZeroException>();

        var originalFail = Result.Fail<int>(new NotSupportedException("bad"));
        var chained = originalFail.Bind(x => Div(x, 2));
        _ = chained.IsFailure.Should().BeTrue();
        _ = chained.Error.Message.Should().Be("bad");
    }

    [TestMethod]
    public void MapError_Failure_ReplacesError()
    {
        var original = Result.Fail<int>(new InvalidOperationException("x"));
        var wrapped = original.MapError(e => new NotSupportedException("wrap", e));

        _ = wrapped.IsFailure.Should().BeTrue();
        _ = wrapped.Error.Should().BeOfType<NotSupportedException>();
        _ = wrapped.Error.InnerException.Should().Be(original.Error);
    }

    [TestMethod]
    public void GetOrElse_Success_ReturnsValue_Failure_ReturnsFallback()
    {
        var ok = Result.Ok("yes");
        _ = ok.GetOrElse("no").Should().Be("yes");

        var fail = Result.Fail<string>(new NotSupportedException());
        _ = fail.GetOrElse("no").Should().Be("no");

        _ = fail.GetOrElse(e => e.Message.Length > 0 ? "x" : "y").Should().Be("x");
    }

    [TestMethod]
    public void Tap_InvokesAppropriateActions_AndReturnsOriginal()
    {
        var ok = Result.Ok(7);
        var called = false;
        var returned = ok.Tap(v => called = v == 7, _ => throw new InvalidOperationException("should not"));

        _ = called.Should().BeTrue();
        _ = returned.Equals(ok).Should().BeTrue();

        var ex = new NotSupportedException("err");
        var fail = Result.Fail<int>(ex);
        var calledFail = false;
        var returnedFail = fail.Tap(_ => throw new InvalidOperationException("should not"), e => calledFail = ReferenceEquals(e, ex));

        _ = calledFail.Should().BeTrue();
        _ = returnedFail.Equals(fail).Should().BeTrue();
    }

    [TestMethod]
    public void Deconstruct_YieldsComponents()
    {
        var ok = Result.Ok("hi");
        var (isSuccess, val, err) = ok;
        _ = isSuccess.Should().BeTrue();
        _ = val.Should().Be("hi");
        _ = err.Should().BeNull();

        var ex = new NotSupportedException("e");
        var fail = Result.Fail<string>(ex);
        var (isSuccess2, val2, err2) = fail;
        _ = isSuccess2.Should().BeFalse();
        _ = val2.Should().BeNull();
        _ = err2.Should().BeSameAs(ex);
    }

    [TestMethod]
    public void Equals_SuccessVsFailure_NotEqual()
    {
        var ok = Result.Ok(5);
        var fail = Result.Fail<int>(new InvalidOperationException("boom"));

        // different success state => not equal
        _ = ok.Equals(fail).Should().BeFalse();
        _ = (ok == fail).Should().BeFalse();
        _ = (ok != fail).Should().BeTrue();
    }

    [TestMethod]
    public void Equals_Object_NullOrOtherType_ReturnsFalse()
    {
        var ok = Result.Ok(10);

        object otherType = 10; // boxed int, not Result<int>

        _ = ok.Equals(null).Should().BeFalse();
        _ = ok.Equals(otherType).Should().BeFalse();
    }

    [TestMethod]
    public void GetOrElse_Factory_NotInvoked_OnSuccess()
    {
        var ok = Result.Ok("present");
        var called = false;
        string F(Exception ex)
        {
            called = true;
            return "fallback";
        }

        var res = ok.GetOrElse(F);

        _ = res.Should().Be("present");
        _ = called.Should().BeFalse();
    }

    [TestMethod]
    public void Tap_Failure_WithNullOnFailure_DoesNotCallOnSuccess()
    {
        var ex = new InvalidOperationException("boom");
        var fail = Result.Fail<int>(ex);

        var successCalled = false;

        // onFailure is null; onSuccess should not be called and Tap should return original result
        var returned = fail.Tap(_ => successCalled = true, onFailure: null);

        _ = successCalled.Should().BeFalse();
        _ = returned.Equals(fail).Should().BeTrue();
    }

    [TestMethod]
    public void OperatorInequality_DifferentValues_ReturnsTrue()
    {
        var a = Result.Ok(1);
        var b = Result.Ok(2);

        _ = (a != b).Should().BeTrue();
    }

    [TestMethod]
    public void Equals_Object_BoxedResult_Works()
    {
        var a = Result.Ok("x");
        object boxed = a;

        _ = a.Equals(boxed).Should().BeTrue();
        _ = boxed.Equals(a).Should().BeTrue();
    }

    [TestMethod]
    public void GetHashCode_Failure_DoesNotThrow()
    {
        var ex = new InvalidOperationException("err");
        var fail = Result.Fail<string>(ex);
        Action act = () => _ = fail.GetHashCode();
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    public void MapError_Success_NoOp()
    {
        var ok = Result.Ok(5);
        var mapped = ok.MapError(e => new InvalidOperationException("wrap", e));
        _ = mapped.IsSuccess.Should().BeTrue();
        _ = mapped.Value.Should().Be(5);
    }

    [TestMethod]
    public void Map_Success_CanReturnNullValue()
    {
        var ok = Result.Ok<string?>("a");
        var mapped = ok.Map<string?>(s => null);
        _ = mapped.IsSuccess.Should().BeTrue();
        _ = mapped.Value.Should().BeNull();
    }
}
