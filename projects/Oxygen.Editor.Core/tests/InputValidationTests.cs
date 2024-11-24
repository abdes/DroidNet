// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Oxygen.Editor.Core;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("File Name Validation")]
public class InputValidationTests
{
    [TestMethod]
    [DataRow("validFileName.txt", true)]
    [DataRow("another_valid-file.name", true)]
    [DataRow("CON", false)]
    [DataRow("PRN", false)]
    [DataRow("AUX", false)]
    [DataRow("NUL", false)]
    [DataRow("COM1", false)]
    [DataRow("LPT1", false)]
    [DataRow("CLOCK$", false)]
    [DataRow("..", false)]
    [DataRow("invalid<name>.txt", false)]
    [DataRow("invalid|name.txt", false)]
    [DataRow("invalid:name.txt", false)]
    [DataRow("invalid/name.txt", false)]
    [DataRow("invalid\\name.txt", false)]
    [DataRow("invalid*name.txt", false)]
    [DataRow("invalid?name.txt", false)]
    [DataRow("invalid\"name.txt", false)]
    [DataRow("invalid\0name.txt", false)]
    [DataRow("invalid\x1F" + "name.txt", false)]
    [DataRow("invalid name.txt ", false)]
    public void IsValidFileName_ShouldReturnExpectedResult(string fileName, bool expectedResult)
    {
        // Act
        var result = InputValidation.IsValidFileName(fileName);

        // Assert
        _ = result.Should().Be(expectedResult);
    }
}
