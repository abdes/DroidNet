// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Converters;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Mvvm;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>
/// Contains test cases for the <see cref="ViewModelToView" /> class.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class ViewModelToViewTests
{
    private readonly ViewModelToView converter;
    private readonly Mock<IViewLocator> viewLocatorMock;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewModelToViewTests" />
    /// class.
    /// </summary>
    /// <remarks>
    /// A new instance of the test class is created for each test case.
    /// </remarks>
    public ViewModelToViewTests()
    {
        this.viewLocatorMock = new Mock<IViewLocator>();
        this.converter = new ViewModelToView(this.viewLocatorMock.Object);
    }

    /// <summary>
    /// Tests that converting a null value should return null.
    /// </summary>
    [TestMethod]
    public void Convert_WhenValueIsNull_ShouldReturnNull()
    {
        var result = this.converter.Convert(value: null, typeof(object), parameter: null, language: null);

        _ = result.Should().BeNull();
    }

    /// <summary>
    /// Tests that converting a non-null value should return the view from the
    /// <see cref="IViewLocator" />.
    /// </summary>
    [TestMethod]
    public void Convert_WhenValueIsNotNull_ShouldReturnView()
    {
        var viewModel = new TestViewModel();
        var view = new Mock<IViewFor<TestViewModel>>().Object;

        _ = this.viewLocatorMock.Setup(v => v.ResolveView(viewModel)).Returns(view);

        var result = this.converter.Convert(viewModel, view.GetType(), parameter: null, language: null);

        _ = result.Should().Be(view);
    }

    /// <summary>
    /// Tests that conversion from a view to a view model is not implemented.
    /// </summary>
    [TestMethod]
    public void ConvertBack_ShouldThrowNotImplementedException()
    {
        var action = () => this.converter.ConvertBack(value: null, targetType: null, parameter: null, language: null);

        _ = action.Should().Throw<InvalidOperationException>();
    }
}

#pragma warning disable MA0048 // File name must match type name
/// <summary>A test view model.</summary>
public class TestViewModel;
#pragma warning restore MA0048 // File name must match type name
