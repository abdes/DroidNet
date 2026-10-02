// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
public sealed class NumericInputParserTests
{
    [TestMethod]
    [DataRow("90/2", 12f, NumericInputOperation.Absolute, 45f, 45f)]
    [DataRow("(90 + 10) / 2", 12f, NumericInputOperation.Absolute, 50f, 50f)]
    [DataRow("+=2", 5f, NumericInputOperation.Add, 2f, 7f)]
    [DataRow("-=2", 5f, NumericInputOperation.Subtract, 2f, 3f)]
    [DataRow("*2", 5f, NumericInputOperation.Multiply, 2f, 10f)]
    [DataRow("/2", 5f, NumericInputOperation.Divide, 2f, 2.5f)]
    [DataRow("*=2", 5f, NumericInputOperation.Multiply, 2f, 10f)]
    public void ValidInputParsesWithItsEditMode(string text, float current, NumericInputOperation operation, float operand, float result)
    {
        var parsed = NumericInputParser.TryParse(text, current, out var expression);

        _ = parsed.Should().BeTrue();
        _ = expression.Operation.Should().Be(operation);
        _ = expression.Operand.Should().Be(operand);
        _ = expression.Apply(current).Should().Be(result);
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("+=")]
    [DataRow("/0")]
    [DataRow("1/0")]
    [DataRow("1+")]
    [DataRow("NaN")]
    [DataRow("Infinity")]
    public void InvalidOrNonFiniteInputIsRejected(string text)
    {
        _ = NumericInputParser.TryParse(text, 5f, out _).Should().BeFalse();
    }
}
