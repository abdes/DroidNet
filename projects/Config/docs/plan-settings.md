---
goal: Implementation plan for the revamped Settings Module based on the comprehensive design specification
version: 1.0
date_created: 2025-10-19
last_updated: 2025-10-19
owner: AI Agent
status: 'Planned'
tags: ['feature', 'architecture', 'settings', 'config', 'infrastructure']
---

# Settings Module Implementation Plan

![Status: In Progress](https://img.shields.io/badge/status-In%20Progress-yellow)
![Phase: 6/10](https://img.shields.io/badge/phase-6%2F10-blue)
![Tests: 79 passing](https://img.shields.io/badge/tests-79%20passing-green)

This implementation plan provides a comprehensive roadmap for developing the revamped Settings Module as specified in
the [Settings Module Design Specification](spec-design-settings.md). The plan follows a phased approach that builds core
abstractions first, then implements concrete sources, followed by integration and testing.

## 1. Requirements & Constraints

Based on the design specification analysis and existing codebase examination:

- **REQ-001**: Implement completely new ISettingsService&lt;TSettings&gt; interface with async operations and comprehensive lifecycle management
- **REQ-002**: Implement ISettingsSource abstraction for pluggable storage backends with atomic write semantics
- **REQ-003**: Implement SettingsManager as central orchestrator with last-loaded-wins strategy for multi-source composition
- **REQ-004**: Replace existing synchronous SettingsService with new async-first implementation following specification contracts
- **REQ-005**: Maintain compatibility with existing DryIoc dependency injection patterns used throughout DroidNet
- **REQ-006**: Use System.Text.Json consistently (already in use in existing codebase)
- **REQ-007**: Leverage existing Testably.Abstractions for file system testing

- **SEC-001**: Implement Secret&lt;T&gt; wrapper type with proper encryption support for sensitive configuration values
- **SEC-002**: Ensure atomic file operations prevent corruption during concurrent access scenarios

- **CON-001**: Must work with .NET 9.0 target framework as specified in existing Config.csproj
- **CON-002**: Must integrate with existing project structure and maintain namespace DroidNet.Config
- **CON-003**: Cannot break existing consumers during transition period - implement side-by-side initially
- **CON-004**: Must use existing logging infrastructure (Microsoft.Extensions.Logging.Abstractions)

- **GUD-001**: Follow established patterns from existing codebase (async/await, disposable, property change notifications)
- **GUD-002**: Maintain separation of concerns between sources (I/O) and services (business logic)
- **GUD-003**: Use explicit interface segregation for different responsibilities

## 2. Implementation Steps

### Implementation Phase 1: Core Abstractions and Contracts

- GOAL-001: Establish foundational interfaces and core types required by all other components

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-001 | Create `ISettingsSource` interface in `src/ISettingsSource.cs` with async operations, metadata support, atomic writes, and file watching capabilities |
|✅| TASK-002 | Create exception types: `SettingsValidationException`, `SettingsPersistenceException` in `src/` folder |
|✅| TASK-003 | Create validation error types: `SettingsValidationError` class in `src/SettingsValidationError.cs` |
|✅| TASK-004 | Create source lifecycle event types: `SettingsSourceChangeType` enum and `SettingsSourceChangedEventArgs` class in `src/Events/` folder |
|✅| TASK-005 | Create `SettingsMetadata` class in `src/SettingsMetadata.cs` for version tracking and migration support |
|✅| TASK-006 | Create result types: `SettingsSourceReadResult`, `SettingsSourceWriteResult`, `SettingsSourceResult` in `src/` folder |
|✅| TASK-007 | Create `Secret<T>` wrapper type in `src/Security/Secret.cs` with proper encapsulation and implicit conversion |
|✅| TASK-008 | Update `ISettingsService<TSettings>` interface in `src/ISettingsService.cs` to match specification with async operations, validation, migration support |

### Implementation Phase 2: Settings Manager and Core Service Implementation

- GOAL-002: Implement central orchestration and base service functionality with multi-section support

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-009 | Create `ISettingsManager` interface in `src/ISettingsManager.cs` with service factory, source management, and lifecycle coordination |
|✅| TASK-010 | Implement `SettingsManager` class in `src/SettingsManager.cs` with source enumeration, section-to-service mapping (using TSettings type name as section key), last-loaded-wins merging across sources, and service instance management |
|✅| TASK-011 | Implement new `SettingsService<TSettings>` class in `src/SettingsService.cs` following specification (replace existing implementation) |
|✅| TASK-012 | Add validation framework integration to SettingsService for property-level validation using data annotations |
|✅| TASK-013 | Implement source lifecycle event handling and propagation in SettingsManager, ensuring all registered services receive section updates from multi-section sources |
|✅| TASK-014 | Add async initialization and reload capabilities to SettingsService with cancellation token support |

### Implementation Phase 3: JSON Settings Source Implementation ✅ COMPLETE

- GOAL-003: Implement file-based JSON storage with atomic operations, multi-section structure, and metadata handling

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-015 | Create `JsonSettingsSource` class in `src/Sources/JsonSettingsSource.cs` implementing ISettingsSource |
|✅| TASK-016 | Implement multi-section JSON structure parsing: metadata section (required) + named settings sections (zero or more), discarding any root-level items outside sections |
|✅| TASK-017 | Implement JSON serialization/deserialization with System.Text.Json for metadata section and individual settings sections |
|✅| TASK-018 | Add atomic write semantics to JsonSettingsSource using temporary file + rename pattern for writing complete multi-section document |
|✅| TASK-019 | Implement file watching capabilities in JsonSettingsSource for external change detection and source reload notifications |
|✅| TASK-020 | Add optional schema validation support to JsonSettingsSource using JSON schema patterns for validating overall file structure |
|✅| TASK-020a | Implement proper error handling and recovery in JsonSettingsSource for corrupted files, missing metadata section, access errors |

**Implementation Status**: ✅ COMPLETE - All core JSON source functionality implemented with comprehensive test coverage (17 unit tests)

NOTE:
All logging must use source generator log methods. Class is injected with ILoggerFactory (refer to existing classes for examples)

### Implementation Phase 4: Encrypted Settings Source

- GOAL-004: Implement secure storage for sensitive configuration data using encryption

| Completed | Task | Description |
|------|-------------|-----------|
| | TASK-021 | Create `EncryptedJsonSettingsSource` class in `src/Sources/EncryptedJsonSettingsSource.cs` extending JsonSettingsSource |
| | TASK-022 | Implement Secret&lt;T&gt; encryption/decryption using platform-appropriate APIs (DPAPI on Windows) |
| | TASK-023 | Add key management and rotation capabilities to EncryptedJsonSettingsSource |
| | TASK-024 | Implement secure memory handling to prevent secret leakage in logs or exceptions |
| | TASK-025 | Add validation to prevent Secret&lt;T&gt; properties from being saved to non-encrypted sources |

### Implementation Phase 5: DryIoc Integration and Bootstrapper

- GOAL-005: Integrate with existing DryIoc dependency injection patterns and provide bootstrapper extensions

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-026 | Create `BootstrapperExtensions` class in `src/BootstrapperExtensions.cs` with `WithSettings` method |
|✅| TASK-027 | Implement file extension to source type mapping (`.json` → JsonSettingsSource, `.secure.json` → EncryptedJsonSettingsSource placeholder) |
|✅| TASK-028 | Add DryIoc factory registration for settings sources using Made.Of pattern to capture file paths |
|✅| TASK-029 | Register SettingsManager as singleton and provide RegisterSettingsService&lt;T&gt; helper for applications to register each settings type |

### Implementation Phase 6: Migration System

- GOAL-006: Implement schema evolution and version upgrade capabilities within existing SettingsService and SettingsManager

| Completed | Task | Description |
|------|-------------|-----------|
| | TASK-031 | Create `SettingsMigrationException` in `src/Exceptions/SettingsMigrationException.cs` for migration-specific error handling |
| | TASK-032 | Implement migration logic within `SettingsService<TSettings>` for type-specific schema evolution and version detection |
| | TASK-033 | Add migration orchestration to `SettingsManager` for coordinating migrations across all services and sources |
| | TASK-034 | Implement version comparison utilities within SettingsManager for determining migration necessity |
| | TASK-035 | Add backup and rollback capabilities to SettingsManager for safe migration execution with data preservation |

### Implementation Phase 7: Unit Testing Infrastructure ✅ MOSTLY COMPLETE

- GOAL-007: Implement comprehensive unit testing for all core components

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-036 | Create test base classes in `tests/TestHelpers/` for settings testing with mock sources and temporary files |
|✅| TASK-037 | Implement unit tests for SettingsService&lt;T&gt; in `tests/SettingsService/NewSettingsServiceTests.cs` covering property access, validation, persistence (14 unit tests) |
|✅| TASK-038 | Implement unit tests for SettingsManager in `tests/SettingsManagerTests.cs` covering source management, last-loaded-wins (15 unit tests) |
|✅| TASK-039 | Implement unit tests for JsonSettingsSource in `tests/Sources/JsonSettingsSourceTests.cs` covering serialization, atomic writes (17 unit tests) |
| | TASK-040 | Implement unit tests for EncryptedJsonSettingsSource in `tests/Sources/EncryptedSourceTests.cs` covering secret encryption |
|✅| TASK-041 | Create validation testing suite in `tests/Validation/ValidationTests.cs` for property-level validation scenarios (15 unit tests) |
| | TASK-042 | Implement unit tests for migration system in `tests/Migration/MigrationTests.cs` covering discovery, execution, rollback |

**Implementation Status**: ✅ MOSTLY COMPLETE - Core testing infrastructure complete with 61+ unit tests. Missing: encrypted source tests and migration tests (features not yet implemented).

**Test Helpers Implemented**:

- `SettingsTestBase` - Base class with DI container setup and file system abstraction
- `MockSettingsSource` - Mock source for testing
- `TestSettings`, `AlternativeTestSettings`, `InvalidTestSettings` - Test data classes
- Test service implementations for all test settings types

NOTES:

- Use Testably abstractions to avoid creating real files.
- Properly setup a DI container with all required services, see `BootstrapperExtensions.cs`

### Implementation Phase 8: Integration Testing ✅ MOSTLY COMPLETE

- GOAL-008: Implement integration testing for multi-component scenarios and real file system operations

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-043 | Create integration test suite in `tests/Integration/DependencyInjectionTests.cs` for testing DI container setup and bootstrapper (18 integration tests) |
| | TASK-044 | Implement concurrency testing in `tests/Integration/ConcurrencyTests.cs` for thread safety validation |
| | TASK-045 | Create file system integration tests in `tests/Integration/FileSystemTests.cs` using Testably.Abstractions |
| | TASK-046 | Implement migration integration tests in `tests/Integration/MigrationTests.cs` for end-to-end migration scenarios |
|✅| TASK-047 | Create DI container integration tests in `tests/Integration/DIIntegrationTests.cs` for bootstrapper validation |

**Implementation Status**: ✅ MOSTLY COMPLETE - DI integration testing complete (18 tests). Missing: concurrency tests, dedicated file system tests, and migration integration tests.

### Implementation Phase 9: Documentation and Examples

- GOAL-009: Create comprehensive documentation and usage examples

| Completed | Task | Description |
|------|-------------|-----------|
| | TASK-048 | Update README.md with new Settings Module overview, quick start guide, and migration instructions |
| | TASK-049 | Create API documentation in `docs/api/` folder with complete interface documentation and examples |
| | TASK-050 | Create migration guide in `docs/migration-guide.md` for upgrading from existing SettingsService |
| | TASK-051 | Create security guide in `docs/security-guide.md` for secret management and encryption best practices |
| | TASK-052 | Create troubleshooting guide in `docs/troubleshooting.md` for common issues and debugging |

### Implementation Phase 10: Performance and Security Testing

- GOAL-010: Validate performance characteristics and security properties

| Completed | Task | Description |
|------|-------------|-----------|
| | TASK-053 | Create performance benchmarks in `tests/Performance/` using BenchmarkDotNet for load operations, saves, property access |
| | TASK-054 | Implement security tests in `tests/Security/` for secret encryption, key management, access control |
| | TASK-055 | Create stress testing suite for concurrent access patterns and file system edge cases |
| | TASK-056 | Implement memory leak detection tests for long-running settings services with file watching |
| | TASK-057 | Add compliance validation tests for encryption standards and audit logging requirements |

## 3. Alternatives

The following alternative approaches were considered during specification analysis:

- **ALT-001**: Extend existing SettingsService instead of complete rewrite - Rejected due to fundamental architectural differences requiring async operations, multi-source support, and atomic writes
- **ALT-002**: Use Microsoft.Extensions.Configuration directly without custom abstraction - Rejected due to lack of property change notifications, validation, and atomic write requirements
- **ALT-003**: Implement precedence-based source merging instead of last-loaded-wins - Rejected to simplify implementation and avoid complex precedence rule configuration
- **ALT-004**: Use binary serialization instead of JSON - Rejected due to human readability requirements and cross-platform compatibility concerns
- **ALT-005**: Implement synchronous API with async implementation underneath - Rejected due to specification requirement for cancellation token support throughout

## 4. Dependencies

Implementation requires the following dependencies and infrastructure:

- **DEP-001**: System.Text.Json (already referenced) - Required for JSON serialization with metadata envelope format
- **DEP-002**: Testably.Abstractions (already referenced) - Required for file system abstraction and testing
- **DEP-003**: Microsoft.Extensions.Logging.Abstractions (already referenced) - Required for diagnostic logging
- **DEP-004**: DryIoc.dll (already referenced) - Required for dependency injection integration
- **DEP-005**: System.ComponentModel.Annotations (add) - Required for validation attributes support
- **DEP-006**: Microsoft.Extensions.Options (already referenced) - Required for options pattern integration
- **DEP-007**: xUnit or MSTest testing framework - Required for unit and integration testing
- **DEP-008**: Moq - Required for mocking in unit tests
- **DEP-009**: FluentAssertions - Required for readable test assertions

## 5. Files

The implementation will affect the following files and directories:

- **FILE-001**: `src/ISettingsService.cs` - Major interface changes for async operations and new capabilities
- **FILE-002**: `src/SettingsService.cs` - Complete rewrite following new specification
- **FILE-003**: `src/ISettingsManager.cs` - New interface for central orchestrator
- **FILE-004**: `src/SettingsManager.cs` - New implementation for source management
- **FILE-005**: `src/Sources/` - New directory with ISettingsSource and implementations
- **FILE-009**: `src/Security/` - New directory with Secret&lt;T&gt; and encryption support
- **FILE-010**: `src/Migration/` - New directory with migration system components
- **FILE-011**: `src/` - Base directory for the rest of the implementation files
- **FILE-012**: `tests/` - Expanded test directory with comprehensive test coverage
- **FILE-013**: `docs/` - Updated documentation and guides

## 6. Testing

Comprehensive testing strategy covering all specification requirements:

- **TEST-001**: Unit tests for SettingsService&lt;T&gt; covering property access, change notifications, validation, persistence
- **TEST-002**: Unit tests for SettingsManager covering source loading, last-loaded-wins merging, service factory
- **TEST-003**: Unit tests for JsonSettingsSource covering JSON serialization, atomic writes, file watching
- **TEST-004**: Unit tests for EncryptedJsonSettingsSource covering secret encryption, key management
- **TEST-005**: Integration tests for multi-source scenarios with real file system operations
- **TEST-006**: Concurrency tests for thread safety under concurrent read/write load
- **TEST-007**: Migration tests for schema evolution and version upgrade scenarios
- **TEST-008**: Security tests for secret encryption, access control, audit logging
- **TEST-009**: Performance tests using BenchmarkDotNet for latency and throughput baselines
- **TEST-010**: DI integration tests for bootstrapper registration and service resolution

## 7. Risks & Assumptions

- **RISK-001**: Breaking changes to existing SettingsService consumers - Mitigate with side-by-side implementation and clear migration guide
- **RISK-002**: Platform-specific encryption differences affecting Secret&lt;T&gt; portability - Mitigate with abstracted encryption provider interface
- **RISK-003**: File system race conditions during atomic writes on network drives - Mitigate with retry logic and proper error handling
- **RISK-004**: Performance impact of file watching on systems with many settings files - Mitigate with configurable file watching and debouncing
- **RISK-005**: Complex DryIoc registration patterns affecting container performance - Mitigate with careful factory registration and lazy initialization

- **ASSUMPTION-001**: Existing consumers can migrate incrementally without breaking production systems
- **ASSUMPTION-002**: File system supports atomic rename operations on all target platforms
- **ASSUMPTION-003**: Platform-specific encryption APIs are available and accessible (DPAPI, keyring, etc.)
- **ASSUMPTION-004**: JSON file sizes remain reasonable (<10MB) for in-memory processing
- **ASSUMPTION-005**: Network file systems support file watching and atomic operations reliably

## 8. Related Specifications / Further Reading

- [Settings Module Design Specification](spec-design-settings.md) - Complete technical specification for this implementation
- [DryIoc Documentation](https://github.com/dadhi/DryIoc) - Container registration patterns and dependency injection
- [System.Text.Json Documentation](https://docs.microsoft.com/en-us/dotnet/standard/serialization/system-text-json-overview) - JSON serialization best practices
- [.NET Data Protection APIs](https://docs.microsoft.com/en-us/aspnet/core/security/data-protection/) - Encryption and key management
- [Testably.Abstractions](https://github.com/Testably/Testably.Abstractions) - File system abstraction for testing

---

## Implementation Status (Updated: October 19, 2025)

### ✅ Core Module Functional

The Settings Module core implementation is **functional and ready for use** with the following capabilities:

- ✅ Multi-source settings management with last-loaded-wins strategy
- ✅ JSON file-based settings persistence with atomic writes
- ✅ Property-level validation using data annotations
- ✅ Change notification and dirty tracking
- ✅ DryIoc integration with bootstrapper extensions
- ✅ Comprehensive test coverage (79 tests, all passing)

### 🚧 Optional Features Pending

The following features are planned but not yet implemented:

- ⏳ Encrypted settings source for sensitive data (Phase 4)
- ⏳ Schema migration and versioning system (Phase 6)
- ⏳ Concurrency and stress testing (Phase 8)
- ⏳ API documentation and migration guides (Phase 9)
- ⏳ Performance benchmarks (Phase 10)

### 🎯 Production Readiness

**Current Status**: Production-ready for basic use cases

The current implementation is suitable for production use in scenarios that require:

- Configuration management from JSON files
- Multi-source configuration composition
- Property validation and change tracking
- Dependency injection integration

For applications requiring encryption, migrations, or high-concurrency scenarios, wait for completion of Phases 4, 6, and 8.
