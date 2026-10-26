// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

/// <summary>Checks the values a drag reaches, step by step.</summary>
[TestClass]
public sealed class NumberBoxStepTests
{
    /// <summary>A step lands on the value its text names, never on the binary rounding of the sum.</summary>
    /// <param name="value">The value the drag starts from.</param>
    /// <param name="steps">The steps it moves.</param>
    /// <param name="step">The change of one step.</param>
    /// <param name="expected">The value it reaches.</param>
    [TestMethod]
    [DataRow(12.2d, 1, 0.1d, 12.3d)]
    [DataRow(0.1d, 2, 0.1d, 0.3d)]
    [DataRow(1d, -3, 0.25d, 0.25d)]
    [DataRow(12.234d, 1, 0.1d, 12.334d)]
    [DataRow(5d, 7, 1d, 12d)]
    public void StepKeepsTheDecimalsOfTheValueAndTheStep(double value, int steps, double step, double expected)
        => _ = NumberBox.Stepped(value, steps, step).Should().Be(expected);

    /// <summary>A float value widened to double steps from the value it shows: 12.2f + 0.1 is 12.3, which reads back
    /// as 12.3f.</summary>
    [TestMethod]
    public void StepFromAWidenedFloatLandsOnTheFloatItShows()
        => _ = ((float)NumberBox.Stepped(12.2f, 1, 0.1d)).Should().Be(12.3f);

    /// <summary>The fine Shift step, a tenth of the multiplier, counts as its shown decimals.</summary>
    [TestMethod]
    public void FineStepCountsItsShownDecimals()
        => _ = NumberBox.Stepped(12.2d, 1, 0.1d * 0.1d).Should().Be(12.21d);
}
