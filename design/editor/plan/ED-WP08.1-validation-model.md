# ED-WP08.1 - Structured Validation Model

Status: `planned`

## 1. Goal

Create a structured validation system for scene, content, cooking, live sync,
and runtime readiness.

## 2. Validation Result Shape

Each result includes:

- code
- severity
- title/message
- scope
- related IDs or paths
- source subsystem
- optional fix action
- timestamp or validation generation

## 3. Scopes

- scene
- node
- component
- asset reference
- descriptor/manifest
- cooked index
- engine runtime
- viewport/surface
- settings

## 4. Producers

- scene authoring validators
- content reference validators
- cook/index validators
- live sync adapters
- runtime engine service
- viewport/surface manager

## 5. Consumers

- inspector field messages
- scene explorer markers
- content browser diagnostics
- validation center panel
- tests and workflow gates

## 6. Acceptance Criteria

- Missing geometry asset shows a validation result.
- Invalid cooked root blocks mount and shows a result.
- Live sync failure keeps authoring state but shows a result.
- Validation center can filter by severity and scope.
- Milestone validation can be recorded from structured results, not only logs.

## 7. Risks

- Existing logging may hide errors that should be validation results.
- Validators need incremental invalidation to avoid expensive full scans.
- UI must avoid overwhelming users with duplicate errors.
