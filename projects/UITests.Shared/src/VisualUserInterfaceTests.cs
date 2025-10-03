// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using FluentAssertions;
using Microsoft.UI.Xaml;

namespace DroidNet.Tests;

/// <summary>
/// A base class for writing visual UI tests that require all UI-related code to be executed on the
/// main thread,and the control being tested to be fully realized in the UI.
/// </summary>
/// <remarks>
/// <para>
/// Unlike the `[UITestMethod]` attribute, which limits the test scope to scenarios that do not
/// require the control being tested to be fully realized in the UI, this class allows for more
/// comprehensive testing. By using the <see cref="LoadTestContentAsync"/> method, it guarantees
/// that the content loaded is fully realized before test assertions are started. This enables
/// testing of content templates, visual states, event handlers that only trigger after the control
/// is loaded, and more.
/// </para>
/// <para><strong>Writing a Visual UI Test</strong></para>
/// Writing visual UI tests with this class involves dispatching all UI-related code to be executed
/// on the main thread using the provided `EnqueueAsync` methods. These methods ensure that actions
/// are performed safely on the UI thread, avoiding the famous `WRONG_THREAD` error. The class
/// provides multiple versions of these methods to handle asynchronous functions, asynchronous
/// actions, and synchronous actions.
/// <para>
/// Additionally, the <see cref="LoadTestContentAsync"/> method is used to load the test content
/// into the UI and wait for it to be fully realized before performing assertions. This is crucial
/// for testing scenarios that depend on the control being fully loaded, such as verifying content
/// templates, visual states, and event handlers.
/// </para>
/// <para><strong>Test Setup and Cleanup</strong></para>
/// The class ensures that each test starts and ends with a clean slate by unloading any existing
/// content before and after each test. This is achieved through the <see cref="NonOverridableTestSetup"/>
/// and <see cref="NonOverridableTestCleanup"/> methods, which call the <see cref="ClearTestContent"/>
/// method to unload the content. Derived classes can customize the setup and cleanup process by
/// overriding the <see cref="TestSetup"/>, <see cref="TestSetupAsync"/>, <see cref="TestCleanup"/>,
/// and <see cref="TestCleanupAsync"/> methods. These methods provide both synchronous and asynchronous
/// versions to accommodate different initialization and cleanup requirements.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <![CDATA[
/// [TestClass]
/// [ExcludeFromCodeCoverage]
/// public class ThumbnailTests : VisualUserInterfaceTests
/// {
///     [TestMethod]
///     public Task Thumbnail_DefaultState_IsDefaultTemplateAsync() => EnqueueAsync(
///     async () =>
///     {
///         // Setup
///         var thumbnail = new Controls.Thumbnail { Content = new TextBlock { Text = "Test" }, };
///
///         // Act
///         await LoadTestContentAsync(thumbnail).ConfigureAwait(true);
///
///         // Assert
///         _ = thumbnail.ContentTemplate.Should().Be(Application.Current.Resources["DefaultThumbnailTemplate"] as DataTemplate);
///     });
/// }
/// ]]>
/// </example>
public class VisualUserInterfaceTests
{
    /// <summary>
    /// Initializes the test environment before each test method is run.
    /// </summary>
    /// <remarks>
    /// This method ensures that each test starts with a clean slate by unloading any existing content.
    /// It also provides an opportunity for derived test classes to run their own setup code by calling
    /// the <see cref="TestSetup"/> and <see cref="TestSetupAsync"/> methods.
    /// </remarks>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [TestInitialize]
    public async Task NonOverridableTestSetup()
    {
        // Make sure every test starts with a clean slate, even if it doesn't use LoadTestContentAsync.
        await EnqueueAsync(ClearTestContent).ConfigureAwait(true);

        // Give a chance to derived test classes to run their own setup code.
        await EnqueueAsync(this.TestSetup).ConfigureAwait(true);
        await EnqueueAsync(this.TestSetupAsync).ConfigureAwait(true);
    }

    /// <summary>
    /// Cleans up the test environment after each test method is run.
    /// </summary>
    /// <remarks>
    /// This method ensures that each test ends with a clean slate by unloading any existing content.
    /// It also provides an opportunity for derived test classes to run their own cleanup code by calling
    /// the <see cref="TestCleanup"/> and <see cref="TestCleanupAsync"/> methods.
    /// </remarks>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [TestCleanup]
    public async Task NonOverridableTestCleanup()
    {
        // Make sure every test ends with a clean slate, even if it doesn't use LoadTestContentAsync.
        await EnqueueAsync(ClearTestContent).ConfigureAwait(true);

        // Give a chance to derived test classes to run their own cleanup code.
        await EnqueueAsync(this.TestCleanup).ConfigureAwait(true);
        await EnqueueAsync(this.TestCleanupAsync).ConfigureAwait(true);
    }

    /// <summary>
    /// Enqueue an asynchronous function to be executed on the main thread.
    /// </summary>
    /// <typeparam name="T">The type of the result produced by the function.</typeparam>
    /// <param name="function">The asynchronous function to be executed.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "exceptions propagated out of async block")]
    protected static Task EnqueueAsync<T>(Func<Task<T>> function)
    {
        {
            var taskCompletionSource = new TaskCompletionSource<T>();
            _ = VisualUserInterfaceTestsApp.DispatcherQueue.EnqueueAsync(
                async () =>
                {
                    try
                    {
                        var result = await function().ConfigureAwait(true);
                        taskCompletionSource.SetResult(result);
                    }
                    catch (Exception ex)
                    {
                        taskCompletionSource.SetException(ex);
                    }
                });
            return taskCompletionSource.Task;
        }
    }

    /// <summary>
    /// Enqueue an asynchronous action to be executed on the main thread.
    /// </summary>
    /// <param name="function">The asynchronous action to be executed.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "exceptions propagated out of async block")]
    protected static Task EnqueueAsync(Func<Task> function)
    {
        var taskCompletionSource = new TaskCompletionSource();
        _ = VisualUserInterfaceTestsApp.DispatcherQueue.EnqueueAsync(
            async () =>
            {
                try
                {
                    await function().ConfigureAwait(true);
                    taskCompletionSource.SetResult();
                }
                catch (Exception ex)
                {
                    taskCompletionSource.SetException(ex);
                }
            });
        return taskCompletionSource.Task;
    }

    /// <summary>
    /// Attempts to enqueue an action to be executed on the main thread.
    /// </summary>
    /// <param name="function">The action to be executed.</param>
    /// <returns><see langword="true"/> if the action was successfully queued; otherwise, <see langword="false"/>.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "exceptions propagated out of dispatcher queue block")]
    protected static Task EnqueueAsync(Action function)
    {
        var taskCompletionSource = new TaskCompletionSource();
        _ = VisualUserInterfaceTestsApp.DispatcherQueue.TryEnqueue(
            () =>
            {
                try
                {
                    function();
                    taskCompletionSource.SetResult();
                }
                catch (Exception ex)
                {
                    taskCompletionSource.SetException(ex);
                }
            });
        return taskCompletionSource.Task;
    }

    /// <summary>
    /// Loads the specified content into the application's content root and waits for it to be fully loaded.
    /// </summary>
    /// <param name="content">The content to be loaded into the application's content root.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <exception cref="Exception">Thrown if an error occurs while loading the content.</exception>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "we can't do anything about exceptions in this method other than fail the test")]
    protected static async Task LoadTestContentAsync(FrameworkElement content)
    {
        var taskCompletionSource = new TaskCompletionSource<object?>();

        _ = VisualUserInterfaceTestsApp.DispatcherQueue.TryEnqueue(
            () =>
            {
                try
                {
                    content.Loaded += OnLoaded;
                    VisualUserInterfaceTestsApp.ContentRoot = content;
                }
                catch (Exception ex)
                {
                    taskCompletionSource.SetException(ex);
                }
            });

        _ = await taskCompletionSource.Task.ConfigureAwait(true);
        Assert.IsTrue(content.IsLoaded);
        return;

        // ReSharper disable once AsyncVoidMethod
        async void OnLoaded(object sender, RoutedEventArgs args)
        {
            _ = sender; // Unused
            _ = args; // Unused

            content.Loaded -= OnLoaded;

            // Wait for first Render pass
            try
            {
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                taskCompletionSource.SetException(ex);
            }

            taskCompletionSource.SetResult(true);
        }
    }

    /// <summary>
    /// Unloads the specified content from the application content root and waits for it to be fully unloaded.
    /// </summary>
    /// <param name="element">The content to be unloaded.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    protected static async Task UnloadTestContentAsync(FrameworkElement element)
    {
        var taskCompletionSource = new TaskCompletionSource<object?>();

        await EnqueueAsync(
            () =>
            {
                element.Unloaded += OnUnloaded;
                VisualUserInterfaceTestsApp.ContentRoot = null;
            }).ConfigureAwait(false);

        _ = await taskCompletionSource.Task.ConfigureAwait(false);
        await EnqueueAsync(() => element.IsLoaded.Should().BeFalse()).ConfigureAwait(true);
        return;

        void OnUnloaded(object sender, RoutedEventArgs args)
        {
            _ = sender; // Unused
            _ = args; // Unused

            element.Unloaded -= OnUnloaded;
            taskCompletionSource.SetResult(null);
        }
    }

    /// <summary>
    /// Performs additional asynchronous setup steps for derived test classes.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    protected virtual Task TestSetupAsync() => Task.CompletedTask;

    /// <summary>
    /// Performs additional setup steps for derived test classes.
    /// </summary>
    protected virtual void TestSetup()
    {
    }

    /// <summary>
    /// Performs additional asynchronous cleanup steps for derived test classes.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    protected virtual Task TestCleanupAsync() => Task.CompletedTask;

    /// <summary>
    /// Performs additional cleanup steps for derived test classes.
    /// </summary>
    protected virtual void TestCleanup()
    {
    }

    /// <summary>
    /// Clears the test content by unloading any existing content from the application content root.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    private static async Task ClearTestContent()
    {
        // Make sure every test starts with a clean slate, even if it doesn't use LoadTestContentAsync.
        if (VisualUserInterfaceTestsApp.ContentRoot is not null)
        {
            await UnloadTestContentAsync(VisualUserInterfaceTestsApp.ContentRoot).ConfigureAwait(true);
        }

        _ = VisualUserInterfaceTestsApp.ContentRoot.Should().BeNull();
    }
}
