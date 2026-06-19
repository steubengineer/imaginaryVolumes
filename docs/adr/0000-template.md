# ADR-NNNN: <Title>

- **Status:** Proposed | Accepted | Superseded by ADR-NNNN | Rejected
- **Date:** YYYY-MM-DD
- **Supersedes:** ADR-NNNN (or "none")

## Context
What problem forces a decision? What constraints (language standard,
performance, platform, dependencies) apply? What did we read (cite prior ADRs)?

## Decision
The single, unambiguous decision. State the contract precisely enough that an
agent with no prior context can implement to it and verify it.

## Contract Specification
The binding, testable details:
- Types / signatures / layouts affected.
- Invariants established (state them as assertable predicates).
- Ownership / lifetime / aliasing / const rules.
- Concurrency guarantees, if any.
- Conventions (units, frames, winding, indexing) if touched.
- Error & failure semantics (what is UB, what throws, what returns an error).

## Consequences
What becomes easier/harder. Performance implications. What this forecloses.

## Alternatives Considered
Each alternative and why it was not chosen. (Prevents re-litigation.)

## Verification
How conformance is checked: specific tests, static assertions, sanitizer runs,
benchmarks, invariant checks.
