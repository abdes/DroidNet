// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Mvvm.Tests;

/// <summary>
/// A host keeps one view per view-model instance for as long as the view model stays in it.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class HostedViewsTests
{
    private readonly HostedViews views = new();
    private readonly CountingConverter converter = new();

    [TestMethod]
    public void GetOrCreate_SameViewModelAgain_ReturnsTheSameView()
    {
        var viewModel = new object();

        var first = this.views.GetOrCreate(viewModel, this.converter);
        var second = this.views.GetOrCreate(viewModel, this.converter);

        _ = second.Should().BeSameAs(first);
        _ = this.converter.Views.Should().HaveCount(1);
    }

    [TestMethod]
    public void GetOrCreate_DistinctViewModels_GetTheirOwnViews()
    {
        var first = new EqualViewModel();
        var second = new EqualViewModel();

        var firstView = this.views.GetOrCreate(first, this.converter);
        var secondView = this.views.GetOrCreate(second, this.converter);

        _ = secondView.Should().NotBeSameAs(firstView);
    }

    [TestMethod]
    public void GetOrCreate_ConverterChanged_KeepsTheView()
    {
        var viewModel = new object();
        var shown = this.views.GetOrCreate(viewModel, this.converter);
        var other = new CountingConverter();

        var view = this.views.GetOrCreate(viewModel, other);

        _ = view.Should().BeSameAs(shown);
        _ = other.Views.Should().BeEmpty();
    }

    [TestMethod]
    public void GetOrCreate_NoView_ConvertsAgainNextTime()
    {
        var viewModel = new object();

        var missing = this.views.GetOrCreate(viewModel, converter: null);
        var view = this.views.GetOrCreate(viewModel, this.converter);

        _ = missing.Should().BeNull();
        _ = view.Should().BeSameAs(this.converter.Views.Single());
    }

    [TestMethod]
    public void RetainOnly_ReleasesTheOtherViews()
    {
        var kept = new object();
        var released = new object();
        var keptView = (TestView)this.views.GetOrCreate(kept, this.converter)!;
        var releasedView = (TestView)this.views.GetOrCreate(released, this.converter)!;

        this.views.RetainOnly([kept]);

        _ = keptView.ViewModel.Should().BeSameAs(kept);
        _ = releasedView.ViewModel.Should().BeNull();
        _ = this.views.GetOrCreate(kept, this.converter).Should().BeSameAs(keptView);
        _ = this.views.GetOrCreate(released, this.converter).Should().NotBeSameAs(releasedView);
    }

    [TestMethod]
    public void RetainOnly_ViewShowingAnotherViewModel_KeepsThatViewModel()
    {
        var released = new object();
        var view = (TestView)this.views.GetOrCreate(released, this.converter)!;
        var other = new object();
        view.ViewModel = other;

        this.views.RetainOnly([]);

        _ = view.ViewModel.Should().BeSameAs(other);
    }

    /// <summary>Makes a new <see cref="TestView"/> for every conversion, as the view-model converter does.</summary>
    private sealed partial class CountingConverter : IValueConverter
    {
        public List<TestView> Views { get; } = [];

        public object Convert(object value, Type targetType, object parameter, string language)
        {
            var view = new TestView { ViewModel = value };
            this.Views.Add(view);
            return view;
        }

        public object ConvertBack(object value, Type targetType, object parameter, string language)
            => throw new InvalidOperationException();
    }

    private sealed class TestView : IViewFor
    {
        public object? ViewModel { get; set; }
    }

    /// <summary>A view model whose instances are all equal, to show views follow the instance, not equality.</summary>
    private sealed class EqualViewModel
    {
        public override bool Equals(object? obj) => obj is EqualViewModel;

        public override int GetHashCode() => 0;
    }
}
