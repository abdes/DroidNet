// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using System.Diagnostics.CodeAnalysis;

/// <summary>Snapshot tests for the <see cref="AutoInjectGenerator" />.</summary>
[TestClass]
[TestCategory(nameof(AutoInjectGenerator))]
[ExcludeFromCodeCoverage]
public class AutoInjectionGeneratorTests : VerifyBase
{
    /// <summary>
    /// Verifies that when a class is properly annotated with a `InjectAs` using a valid lifecycle, the generator
    /// will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_TargetIsClass_JustLifetime()
    {
        const string source = $"""
                               namespace Testing;

                               using Microsoft.Extensions.DependencyInjection;
                               using DroidNet.Hosting.Generators;

                               [InjectAs(ServiceLifetime.Singleton)]
                               public class TestClass;
                               """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when a class is annotated with a `InjectAs` using a valid lifecycle, and with an implementation type
    /// specified, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_TargetIsClass_RedundantImplementationType()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Singleton, ImplementationType = typeof(TestClass))]
                              public class TestClass
                              {
                              }
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when an interface is annotated with a `InjectAs` using a valid lifecycle, and with an implementation type
    /// specified, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_TargetIsInterface_WithImplementationType()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Transient, ImplementationType = typeof(TestClass))]
                              public interface ITestInterface;

                              class TestClass : ITestInterface;
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when an interface is annotated with a `InjectAs` using a valid lifecycle, with an implementation type
    /// specified, and a key, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_TargetIsInterface_WithImplementationTypeAndKey()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Transient, ImplementationType = typeof(TestClass), Key = "key")]
                              public interface ITestInterface;

                              class TestClass : ITestInterface;
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when an interface is annotated with a `InjectAs` using a valid lifecycle, with an implementation type
    /// specified, and a key, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_MultipleTargets()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Transient, ImplementationType = typeof(TestClass), Key = "key")]
                              public interface ITestInterface;

                              [InjectAs(ServiceLifetime.Transient)]
                              class TestClass : ITestInterface;
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when an interface is annotated with a `InjectAs` using a valid lifecycle, with an implementation type
    /// specified, and a key, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_MultipleAnnotations()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Transient, ImplementationType = typeof(TestClass1), Key = "key1")]
                              [InjectAs(ServiceLifetime.Transient, ImplementationType = typeof(TestClass2), Key = "key2")]
                              public interface ITestInterface;

                              [InjectAs(ServiceLifetime.Transient)]
                              class TestClass1 : ITestInterface;

                              [InjectAs(ServiceLifetime.Transient)]
                              class TestClass2 : ITestInterface;
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when an interface is annotated with a `InjectAs` using a valid lifecycle, with an implementation type
    /// specified, and a key, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_NonDefaultConstructor()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Transient)]
                              public class Argument;

                              [InjectAs(ServiceLifetime.Transient)]
                              public class TestClass
                              {
                                  [ActivatorUtilitiesConstructor]
                                  public TestClass(Argument arg)
                                  {
                                  }
                              }
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }

    /// <summary>
    /// Verifies that when an interface is annotated with a `InjectAs` using a valid lifecycle, with an implementation type
    /// specified, and a key, the generator will correctly generate the code to inject that class in the DI.
    /// </summary>
    /// <returns>An asynchronous task for this test case verification.</returns>
    [TestMethod]
    public Task Generate_TargetIsInterface_ImplementationTypeDoesImplementInterface()
    {
        const string source = """
                              namespace Testing;

                              using Microsoft.Extensions.DependencyInjection;
                              using DroidNet.Hosting.Generators;

                              [InjectAs(ServiceLifetime.Transient, ImplementationType=typeof(TestClass))]
                              public interface ITestInterface;

                              [InjectAs(ServiceLifetime.Transient)]
                              public class TestClass;
                              """;

        // Use verify to snapshot test the source generator output!
        var driver = TestHelper.GeneratorDriver(source);
        var runResults = driver.GetRunResult();
        return this.Verify(runResults).UseDirectory("Snapshots");
    }
}
