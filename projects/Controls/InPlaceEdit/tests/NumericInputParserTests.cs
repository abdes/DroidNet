// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
public sealed class NumericInputParserTests
{
    [TestMethod]
    [DataRow("90/2", 12d, NumericInputOperation.Absolute, 45d, 45d)]
    [DataRow("(90 + 10) / 2", 12d, NumericInputOperation.Absolute, 50d, 50d)]
    [DataRow("+=2", 5d, NumericInputOperation.Add, 2d, 7d)]
    [DataRow("-=2", 5d, NumericInputOperation.Subtract, 2d, 3d)]
    [DataRow("*2", 5d, NumericInputOperation.Multiply, 2d, 10d)]
    [DataRow("/2", 5d, NumericInputOperation.Divide, 2d, 2.5d)]
    [DataRow("*=2", 5d, NumericInputOperation.Multiply, 2d, 10d)]
    public void ValidInputParsesWithItsEditMode(string text, double current, NumericInputOperation operation, double operand, double result)
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
        => _ = NumericInputParser.TryParse(text, 5d, out _).Should().BeFalse();
}
