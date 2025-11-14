// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using AwesomeAssertions;
using DroidNet.Resources.Tests.Fakes;

namespace DroidNet.Resources.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ResourceExtensionsTests
{
    private FakeResourceMap applicationResourceMap = null!;
    private FakeResourceMap assemblyResourceMap = null!;
    private Assembly testAssembly = null!;

    [TestInitialize]
    public void Initialize()
    {
        this.applicationResourceMap = CreateApplicationResourceMap();
        this.assemblyResourceMap = CreateAssemblyResourceMap();
        this.testAssembly = typeof(ResourceExtensionsTests).Assembly;

        // Initialize with fake provider
        var fakeProvider = new FakeResourceMapProvider(this.applicationResourceMap, this.assemblyResourceMap);
        ResourceExtensions.Initialize(fakeProvider);
    }

    [TestCleanup]
    public void Cleanup() => ResetResourceExtensionsProvider();

    [TestMethod]
    public void GetLocalized_UsesApplicationMapByDefault()
    {
        var result = "MSG_Hello".GetLocalized();

        _ = result.Should().Be("Hello from MyApp");
    }

    [TestMethod]
    public void Initialize_ThrowsWhenProviderIsNull()
    {
        var act = new Action(() => ResourceExtensions.Initialize(null!));

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void Initialize_CanReplaceExistingProvider()
    {
        var firstResult = "MSG_Goodbye".GetLocalized();
        _ = firstResult.Should().Be("Goodbye from MyApp");

        var replacementAppMap = new FakeResourceMap(new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["Localized/MSG_Goodbye"] = "Override from Replacement",
        });
        var replacementProvider = new FakeResourceMapProvider(replacementAppMap, this.assemblyResourceMap);
        ResourceExtensions.Initialize(replacementProvider);

        var result = "MSG_Goodbye".GetLocalized();

        _ = result.Should().Be("Override from Replacement");
    }

    [TestMethod]
    public void GetLocalized_ThrowsWhenNotInitialized()
    {
        ResetResourceExtensionsProvider();

        var act = new Action(() => "MSG_Hello".GetLocalized());

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void GetLocalized_StringArgumentNullThrows()
    {
        var act = new Action(() => _ = ((string)null!).GetLocalized());

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void GetLocalized_UsesProvidedSubtreeFirst()
    {
        // The provided map must be the Localized subtree for direct key lookup to succeed
        var specialMap = this.applicationResourceMap.GetSubtree("Special").GetSubtree("Localized");
        var result = "MSG_Special".GetLocalized(specialMap);

        _ = result.Should().Be("Special message from MyApp");
    }

    [TestMethod]
    public void GetLocalized_FallsBackToApplicationMapWhenProvidedMapMissesKey()
    {
        // Providing a map performs direct key lookup only; missing keys return the original value
        var specialMap = this.applicationResourceMap.GetSubtree("Special");
        var result = "MSG_Hello".GetLocalized(specialMap);

        _ = result.Should().Be("MSG_Hello");
    }

    [TestMethod]
    public void GetLocalized_ReturnsOriginalValueWhenNothingMatches()
    {
        var result = "MSG_Unknown".GetLocalized();

        _ = result.Should().Be("MSG_Unknown");
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyAndProvidedMapUsesProvidedMapFirst()
    {
        // Use the Localized subtree under the Special subtree to enable direct key lookup
        var specialAssemblyMap = this.assemblyResourceMap.GetSubtree("Special").GetSubtree("Localized");
        var result = "SPEC_Feature".GetLocalized(this.testAssembly, specialAssemblyMap);

        _ = result.Should().Be("Feature text from LibraryX (special map)");
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyFallsBackToAssemblyMapWhenProvidedMapMisses()
    {
        var specialMap = this.applicationResourceMap.GetSubtree("Special");
        var result = "MSG_Thanks".GetLocalized(this.testAssembly, specialMap);

        _ = result.Should().Be("Thanks");
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyUsesAssemblyMapWhenNoMapProvided()
    {
        var result = "MSG_Hello".GetLocalized(this.testAssembly);

        _ = result.Should().Be("Hello from LibraryX");
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyFallsBackToApplicationMap()
    {
        var result = "MSG_Goodbye".GetLocalized(this.testAssembly);

        _ = result.Should().Be("Goodbye from MyApp");
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyReturnsOriginalWhenAllLookupsFail()
    {
        var result = "MSG_Unknown".GetLocalized(this.testAssembly);

        _ = result.Should().Be("MSG_Unknown");
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyContinuesWhenMapThrows()
    {
        var result = "MSG_Hello".GetLocalized(this.testAssembly, new ThrowingResourceMap());

        _ = result.Should().Be("Hello from LibraryX");
    }

    [TestMethod]
    public void GetLocalized_GenericOverloadResolvesAssembly()
    {
        var result = "MSG_Hello".GetLocalized<ResourceExtensionsTests>();

        _ = result.Should().Be("Hello from LibraryX");
    }

    [TestMethod]
    public void GetLocalized_GenericNullValueThrows()
    {
        var act = new Action(() => _ = ((string)null!).GetLocalized<ResourceExtensionsTests>());

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyNullValueThrows()
    {
        var act = new Action(() => _ = ((string)null!).GetLocalized(this.testAssembly));

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void GetLocalized_WithAssemblyNullAssemblyThrows()
    {
        var act = new Action(() => _ = "MSG_Hello".GetLocalized((Assembly)null!));

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void L_GenericUsesAssemblyLocalizedSubtree()
    {
        var result = "MSG_Hello".L<ResourceExtensionsTests>();

        _ = result.Should().Be("Hello from LibraryX");
    }

    [TestMethod]
    public void L_GenericFallsBackToApplicationMap()
    {
        var result = "MSG_Goodbye".L<ResourceExtensionsTests>();

        _ = result.Should().Be("Goodbye from MyApp");
    }

    [TestMethod]
    public void R_GenericParsesSubtreePaths()
    {
        var result = "Special/Localized/SPEC_Feature".R<ResourceExtensionsTests>();

        _ = result.Should().Be("Feature text from LibraryX (special map)");
    }

    [TestMethod]
    public void R_GenericFallsBackToApplicationMap()
    {
        var result = "Special/Localized/MSG_Special".R<ResourceExtensionsTests>();

        _ = result.Should().Be("Special message from MyApp");
    }

    private static FakeResourceMap CreateApplicationResourceMap()
    {
        var store = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["Localized/MSG_Hello"] = "Hello from MyApp",
            ["Localized/MSG_Goodbye"] = "Goodbye from MyApp",
            ["Special/Localized/MSG_Special"] = "Special message from MyApp",
        };

        return new FakeResourceMap(store);
    }

    private static FakeResourceMap CreateAssemblyResourceMap()
    {
        var store = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["Localized/MSG_Hello"] = "Hello from LibraryX",
            ["Localized/MSG_Thanks"] = "Thanks",
            ["Special/Localized/SPEC_Feature"] = "Feature text from LibraryX (special map)",
        };

        return new FakeResourceMap(store);
    }

    private static void ResetResourceExtensionsProvider()
    {
        var providerField = typeof(ResourceExtensions).GetField("provider", BindingFlags.NonPublic | BindingFlags.Static);
        providerField?.SetValue(null, null);
    }
}
