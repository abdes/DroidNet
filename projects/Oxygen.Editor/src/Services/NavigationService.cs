// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT).
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Oxygen.Editor.Helpers;
using Oxygen.Editor.ViewModels;

// For more information on navigation between pages see
// https://github.com/microsoft/TemplateStudio/blob/main/docs/WinUI/navigation.md
public class NavigationService : INavigationService
{
    private readonly IPageService pageService;
    private Frame? frame;
    private object? lastParameterUsed;

    /// <summary>
    /// Initializes a new instance of the <see cref="NavigationService" />
    /// class.
    /// </summary>
    /// <param name="pageService"></param>
    public NavigationService(IPageService pageService)
        => this.pageService = pageService;

    public event NavigatedEventHandler? Navigated;

    public Frame? Frame
    {
        get
        {
            if (this.frame == null)
            {
                this.frame = App.MainWindow.Content as Frame;
                this.RegisterFrameEvents();
            }

            return this.frame;
        }

        set
        {
            this.UnregisterFrameEvents();
            this.frame = value;
            this.RegisterFrameEvents();
        }
    }

    [MemberNotNullWhen(true, nameof(Frame), nameof(frame))]
    public bool CanGoBack => this.Frame != null && this.Frame.CanGoBack;

    public bool GoBack()
    {
        if (this.CanGoBack)
        {
            var vmBeforeNavigation = this.frame.GetPageViewModel();
            this.frame.GoBack();
            if (vmBeforeNavigation is INavigationAware navigationAware)
            {
                navigationAware.OnNavigatedFrom();
            }

            return true;
        }

        return false;
    }

    public bool NavigateTo(string pageKey, object? parameter = null, bool clearNavigation = false)
    {
        var pageType = this.pageService.GetPageType(pageKey);

        if (this.frame == null || (this.frame.Content?.GetType() == pageType &&
                                   (parameter == null || parameter.Equals(this.lastParameterUsed))))
        {
            return false;
        }

        this.frame.Tag = clearNavigation;
        var vmBeforeNavigation = this.frame.GetPageViewModel();
        var navigated = this.frame.Navigate(pageType, parameter);
        if (navigated)
        {
            this.lastParameterUsed = parameter;
            if (vmBeforeNavigation is INavigationAware navigationAware)
            {
                navigationAware.OnNavigatedFrom();
            }
        }

        return navigated;
    }

    private void RegisterFrameEvents()
    {
        if (this.frame != null)
        {
            this.frame.Navigated += this.OnNavigated;
        }
    }

    private void UnregisterFrameEvents()
    {
        if (this.frame != null)
        {
            this.frame.Navigated -= this.OnNavigated;
        }
    }

    private void OnNavigated(object sender, NavigationEventArgs e)
    {
        Debug.Assert(
            this.frame != null && sender is Frame senderFrame && senderFrame == this.frame,
            "Expecting the sender to be my frame.");

        var clearNavigation = (bool)this.frame.Tag;
        if (clearNavigation)
        {
            this.frame.BackStack.Clear();
        }

        if (this.frame.GetPageViewModel() is INavigationAware navigationAware)
        {
            navigationAware.OnNavigatedTo(e.Parameter);
        }

        this.Navigated?.Invoke(sender, e);
    }
}
