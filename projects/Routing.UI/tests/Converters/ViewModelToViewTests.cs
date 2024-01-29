// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.UI.Converters;

using DroidNet.Routing.View;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>
/// Contains test cases for the <see cref="ViewModelToView" /> class.
/// </summary>
[TestClass]
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
        var result = this.converter.Convert(null, typeof(object), null, null);

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

        var result = this.converter.Convert(viewModel, view.GetType(), null, null);

        _ = result.Should().Be(view);
    }

    /// <summary>
    /// Tests that conversion from a view to a view model is not implemented.
    /// </summary>
    [TestMethod]
    public void ConvertBack_ShouldThrowNotImplementedException()
    {
        var action = () => this.converter.ConvertBack(null, null, null, null);

        _ = action.Should().Throw<NotImplementedException>();
    }
}

/// <summary>A test view model.</summary>
public class TestViewModel;
