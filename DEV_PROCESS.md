# DEV_PROCESS.md

**Status:** Authoritative. This document governs how all agents (human and AI) develop within this project. It is itself subject to the ADR process: changing this process requires an accepted ADR.

This is the **standalone** process — for a single, self-contained C++ project that does not share a binding data model with a larger ecosystem. It keeps the discipline that makes software reliable (explicit contracts, append-only decisions, tests that have teeth, no lost context) without the cross-project coupling machinery a tightly-interrelated ecosystem needs.

---

## 0. Purpose & First Principles

Three principles override all other guidance. When in doubt, resolve toward these:

1. **Contracts are explicit and append-only.** Every binding decision — public interface, invariant, ownership rule, concurrency rule — is recorded as an Architecture Decision Record (ADR). ADRs are never edited after acceptance; they are *superseded* by new ADRs.
2. **The repository is the only memory.** No decision, rationale, or invariant lives solely in a chat session, a person's head, or a model's context window. If it isn't in the repo, it does not exist.
3. **Nothing is "done" until it is proven.** A claim that code works, that a test has teeth, or that a contract holds must be *demonstrated*, not asserted.

> **Agent directive:** You have no reliable memory across sessions. Treat every task as if you are seeing the codebase for the first time. Read before you write. Cite the ADR you are honoring. Never infer a contract you can read.

---

## 1. The ADR System (Append-Only Contracts)

### 1.0 Status model

ADRs live in `/docs/adr/` as numbered files: `NNNN-short-slug.md`, where `NNNN` is zero-padded and strictly increasing. The number is permanent and never reused.

An ADR has exactly one status at any time:

- **Proposed** — drafted, under review, not yet binding.
- **Accepted** — binding. Code MUST conform.
- **Superseded** — was Accepted, now replaced by a later ADR. Still binding for the historical record; no longer governs current code. Its body is unchanged; only its status line and a "Superseded by ADR-NNNN" pointer are added.
- **Rejected** — proposed but declined. Kept for the record so the same idea isn't re-litigated.

> **Append-only means:** the *content* of an Accepted or Rejected ADR is immutable. You may only (a) append the status transition and the supersession pointer, or (b) author a *new* ADR. You may never rewrite history.

### 1.1 When an ADR is required

An ADR is **mandatory** before merging any change that:

- Adds, removes, or changes a **public interface** (anything a consumer of this project can depend on).
- Establishes or changes an **invariant** (a property code may assume always holds).
- Establishes or changes **ownership, lifetime, aliasing, or `const`-correctness** rules for a type that crosses an API boundary.
- Establishes or changes a **concurrency contract** (thread-safety guarantees, synchronization strategy; and, where applicable, GPU memory ownership/transfer semantics).
- Fixes a **convention** the rest of the code must follow (units, coordinate frame, winding, indexing base, error/throwing policy).
- Introduces a new third-party dependency.
- Changes the build, ABI, or versioning policy.

An ADR is **not** required for: internal implementation details with no public surface, private helpers, tests, docs, or refactors that provably preserve every contract (preserved contracts must be asserted, not assumed).

### 1.2 ADR template

Every ADR uses this structure (also stored as `/docs/adr/0000-template.md`):

```markdown
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
```

### 1.3 Authoring discipline

- **Read first.** Before proposing an ADR, read the ADR index (§1.4) and every Accepted ADR touching the area you will change. Cite them in *Context*. Do not propose anything that contradicts an Accepted ADR without explicitly superseding it.
- **One decision per ADR.** Keep ADRs atomic; bundling makes supersession messy.
- **Specify to the point of testability.** If a reader cannot write a test from your *Contract Specification*, it is underspecified.
- **Never edit an Accepted ADR to "fix" it.** Supersede it.

### 1.4 ADR index

`/docs/adr/INDEX.md` is a mechanically regenerated table of every ADR: number, title, status, supersession chain. It is never hand-curated, so it cannot drift from the ADR files. Treat the index as the entry point; treat the ADR files as truth.

---

## 2. Project Structure & Milestone Discipline

This project is anchored by a small set of root artifacts created at birth and maintained throughout. The binding rules for each live in the subsection named.

### 2.0 Project initialization checklist

Create the governing artifacts in this order before writing implementation code. The order is not arbitrary — each artifact depends on the ones before it.

1. **Establish ADR scaffolding.** Create `/docs/adr/` with the template (`0000-template.md`) and a generated `INDEX.md` (§1.2, §1.4).
2. **Author `MILESTONES.md`.** Define the ordered milestone arc and review it. The project does not exist until this does (§2.1).
3. **Create `DECISIONS.md`.** Journal any founding decisions with rationale; seed the Backlog section (§2.8). Create the file even if the initial log is short.
4. **Seed `HANDOFF.md`.** Shortly after the milestones are finalized, point it at the active milestone (M1) and its first task, so the artifact always exists (§2.7).
5. **Confirm the entry point.** A fresh agent's first reads — `HANDOFF.md` → `DEV_PROCESS.md` → `MILESTONES.md` → ADR `INDEX.md` → `DECISIONS.md` (§3.0) — must all resolve. If any required artifact is missing, initialization is incomplete; do not begin implementation.

> **Agent directive:** If asked to implement in a project where any of these artifacts is missing, you are in an **uninitialized project**. Stop and complete §2.0 first.

### 2.1 Project initiation: `MILESTONES.md` first

> **A project does not exist until it has a `MILESTONES.md`.** No public API and no implementation code is written before this artifact is authored and reviewed. It is the first commit of substance.

`MILESTONES.md` lives at the project root and encodes the **specific, high-level milestone goals** the project must reach, **in sequence**.

### 2.2 What a milestone is (and how big it may be)

A **milestone** is a coherent, demonstrable increment of capability — something you could show working and reason about as a unit. Milestones are ordered; later ones may depend on earlier ones, never the reverse.

Sizing rules:

- **Every milestone generates at least one ADR.** Reaching a milestone means making and recording binding decisions. A milestone producing zero ADRs was either trivial (fold it into a neighbor) or its decisions went unrecorded (a defect).
- **A milestone should be coverable by a *few* ADRs — not many.** The ADR count is a sizing signal. A milestone trending past ~3–5 ADRs should be split into sequential sub-milestones. The principle, not the number, governs.
- **Every milestone generates unit tests, and those tests must have teeth** (§2.4).

### 2.3 `MILESTONES.md` structure

```markdown
# MILESTONES.md — <Project Name>

**Status:** Living (see §2.5). Completed milestones are locked.

## Milestone Overview
A one-line statement of each milestone, in execution order.

1. M1 — <short goal>
2. M2 — <short goal>
...

## M1 — <Goal Title>
- **Status:** Planned | In Progress | Complete
- **Goal:** What capability exists when this is done. Demonstrable.
- **Done when:** Concrete, checkable completion criteria.
- **Expected ADRs:** The decisions anticipated. At least one.
- **Tests with teeth:** What the tests will pin down, and how their teeth
  will be shown (§2.4).
- **Actual ADRs:** (filled at completion) ADR-NNNN, ...
```

### 2.4 "Tests with teeth" — operational definition

A test has **teeth** if and only if **it fails when the contract it guards is violated.** A test that passes against a broken or stubbed implementation has no teeth and is worse than no test — it manufactures unearned confidence.

To be accepted, a milestone's tests must satisfy all of:

- **Fault-sensitive.** Each test pins a specific behavior or invariant. There must be a plausible wrong implementation it would catch. If you cannot describe a defect the test would detect, it is decorative — fix or remove it.
- **Demonstrated, not asserted.** Teeth are *shown*: either a recorded **red→green** transition, or **fault injection** (deliberately break the implementation — flip a comparison, perturb a constant, skip a synchronization — show the test goes red, then restore).
- **Non-tautological.** No assertions that cannot fail. Tests exercise the real code path, not a mock that returns the expected answer.
- **Contract-aligned.** Tests assert the conventions and invariants named in the governing ADRs, not just incidental behavior.
- **Meaningful for concurrent/GPU code.** For parallel or device code, "teeth" includes sensitivity to races and ordering: tests run under the appropriate sanitizer/checker and across enough iterations/configurations that a real race has a real chance to surface (§5, §6). A concurrency test that can only ever pass has no teeth.

> **Agent directive:** When you claim a test has teeth, show the evidence in the PR/changelog — name the fault you injected (or the red→green you observed) and the test that caught it. An unproven claim of teeth is treated as toothless.

### 2.5 Lifecycle of `MILESTONES.md` (living, but completed milestones lock)

`MILESTONES.md` is **living** — future milestones may be refined, reordered, split, or added as understanding improves. This does not conflict with append-only ADRs: ADRs record *decisions already made*; `MILESTONES.md` records *intended future arc*. The one constraint: **a milestone marked Complete is locked** — its *Goal*, *Done when*, and *Actual ADRs* are frozen. If a completed milestone turns out wrong, add a new milestone (and the superseding ADRs that go with it) rather than rewriting it.

### 2.6 Milestone completion gate

A milestone is **Complete** only when:

- [ ] Its *Done when* criteria are all met and checkable.
- [ ] It produced **at least one Accepted ADR**, recorded in *Actual ADRs*.
- [ ] Its ADR count stayed small; if it sprawled, it was split rather than forced through (§2.2).
- [ ] It produced unit tests whose **teeth are demonstrated**, not merely claimed (§2.4).
- [ ] All quality gates pass (§7, Definition of Done).
- [ ] The milestone is marked Complete and thereby locked (§2.5).

### 2.7 `HANDOFF.md` — capturing in-flight state

`MILESTONES.md` and the ADRs describe what is *settled*. Neither captures the **volatile present** — what is half-finished, what the last session was mid-way through, what is temporarily broken and why. That is the job of `HANDOFF.md`.

> **Purpose:** let a fresh agent resume work *exactly where the last one stopped* — without re-discovering context or stepping on work in progress.

- **Ephemeral — the opposite of append-only.** It describes *this moment only* and is rewritten continuously. Do not let it become a log; stale or historical content belongs in ADRs, the changelog, or completed-milestone records. A bloated `HANDOFF.md` has failed at its one job.
- **Points to truth; does not duplicate it.** Reference ADRs and milestones; never restate them. If something in the handoff is a settled decision, move it to an ADR and link it.
- **Seeded at initiation; read first, written last.** It is the first thing a resuming agent reads (§3.0) and the last thing a departing agent writes (§3.4). Changing project state but leaving `HANDOFF.md` untouched blinds the next agent — a defect (§8).
- **Always current or explicitly empty.** If nothing is in flight, it says so plainly. An honest empty handoff is correct; a stale one is a defect.

```markdown
# HANDOFF.md — <Project Name>

**Last updated:** YYYY-MM-DD by <agent/session id>
**Active milestone:** M<N> — <title>

## Current State
Where the project actually is right now. What works, what is partial,
what is untouched.

## In Flight (work started, not finished)
- What is mid-implementation, in which files; the intent (and ADR by number);
  how far it got; what remains.

## Next Action
The single most immediate next step, concrete enough to act on.

## Known-Broken / Blocked
- What is failing, disabled, stubbed, or worked-around — and why.
- Blockers, open questions, decisions awaiting an ADR.
- Tests intentionally red and the reason (e.g. red→green in progress).

## Landmines & Context
Non-obvious things that will bite a fresh agent: a subtle invariant held by
hand, a sequencing dependency, a reason something was NOT done the obvious way.

## Pointers
- Governing ADRs for current work: ADR-NNNN, ...
- Relevant files/entry points.
```

### 2.8 `DECISIONS.md` — the decision journal and backlog

`DECISIONS.md` is the running record of **architecturally significant choices** and the rationale that drove them, plus a **Backlog** of choices identified but not taken and review concerns not yet addressed.

It is lighter-weight than an ADR. ADRs are *binding contracts*; `DECISIONS.md` captures reasoning so a future agent does not relitigate settled questions or silently reverse them. A choice with public-contract impact (§1.1) requires an **ADR** and is *also* journaled here (referencing the ADR). A choice that is significant but has no contract surface — e.g. picking between two reasonable internal algorithms with identical public behavior — is journaled here and may never need an ADR.

> **Rule of thumb:** if a consumer of this project could rely on the choice, it needs an ADR. If only the internals feel it, the journal may suffice. When in doubt, write the ADR.

**Record a decision** whenever you reach an architecturally significant choice with more than one reasonable option. Each entry captures: the **choice** (and options considered), the **decision** taken, the **driving rationale** (the point of the entry), an **ADR link** if applicable, and any deferred alternative sent to the Backlog.

**The Backlog** holds two kinds of item: *roads not taken* (a rejected alternative that may deserve reconsideration, cross-linked to the decision that deferred it) and *review concerns not addressed immediately* (a concern of note that is neither fixed on the spot nor lost). A concern dropped without being fixed or backlogged is a silent omission and a defect (§8).

**Removing a backlog item** is never silent — it is a recorded resolution:

- **Resolution by reversal — superseding ADR REQUIRED.** If removal changes, reverses, or newly commits to a decision an ADR governs (or *should* govern under §1.1), author a superseding ADR before the item leaves the Backlog.
- **Resolution without reversal — recorded reason, no ADR.** If the item became *moot* (the code it concerned was deleted, it was already handled, it rested on a misreading), a short dated note suffices. Manufacturing empty ADRs for non-decisions pollutes the record.

> **The deciding test:** *Does removing this item change a decision that code is entitled to rely on?* Yes → superseding ADR. No → recorded mootness note. **When uncertain, treat it as a reversal and write the ADR.**

```markdown
# DECISIONS.md — <Project Name>

## Decision Log
(Newest first.)

### D-NNNN — <Short title>
- **Date / milestone:** YYYY-MM-DD / M<N>
- **Choice:** What was being decided; the reasonable options.
- **Decision:** The option taken.
- **Rationale:** Why — the constraints and trade-offs that drove it.
- **Contract impact:** ADR-NNNN, or "none (internal only)".
- **Deferred alternatives:** → Backlog B-NNNN (if any).

---

## Backlog

### B-NNNN — <Short title>
- **Origin:** D-NNNN (road not taken) | review on YYYY-MM-DD | other.
- **What:** The deferred alternative or raised concern.
- **Why deferred:** Why not taken/addressed now.
- **Revisit when:** A trigger or milestone that makes this worth re-examining.
- **Contract link:** the ADR/decision this would affect if acted on (if any).
```

---

## 3. The Development Loop

Every task follows this loop. Do not skip steps. Do not reorder them.

### 3.0 ORIENT (read before write)
1. **Read `HANDOFF.md` first.** It tells you where the last session stopped, what is in flight, and the next action (§2.7). Never assume a clean state.
2. Read this `DEV_PROCESS.md`.
3. Read `MILESTONES.md` to locate the active milestone and confirm your task serves it.
4. Read `/docs/adr/INDEX.md` and every Accepted ADR touching the area you will change.
5. Read `DECISIONS.md` to see which significant choices are settled (do not relitigate) and what sits in the Backlog (§2.8).
6. Restate, in your task notes, which ADRs and conventions govern this task. If you cannot name them, you are not ready to write code.

### 3.1 CONTRACT (decide before building)
- If the task needs a new or changed contract (per §1.1), **stop and author an ADR** (Proposed). Get it Accepted before implementing. Do not write contract-establishing code ahead of its ADR.
- If no contract change is needed, record explicitly: "No ADR required; conforms to ADR-NNNN." This statement is mandatory even when trivially true.
- **Journal any architecturally significant choice in `DECISIONS.md`** (§2.8): the choice, the decision, the rationale. Send deferred alternatives to the Backlog.

### 3.2 IMPLEMENT (build to the contract)
- Encode every invariant as a checkable assertion (`static_assert` where possible; runtime asserts/contracts at boundaries otherwise). Invariants that aren't asserted rot.
- Keep public surface minimal. Anything public is a contract and may require an ADR.
- Where you integrate a third-party library with different conventions, normalize at the boundary; don't let foreign conventions leak through the codebase.

### 3.3 VERIFY (prove the contract holds)
- Unit tests for behavior; **contract tests** that assert the invariants and conventions named in the governing ADRs.
- Run the full sanitizer/analysis gate (§7).
- For parallel/GPU code: run race/sync verification (§6, §7). "Passes once" is not "is correct" for concurrent code.
- Benchmarks where an ADR set a performance contract.

### 3.4 RECORD (close the loop)
- Update the changelog/PR description with: task summary, governing ADRs, any new ADR numbers, and how verification was performed.
- If reality contradicts an Accepted ADR, do **not** quietly diverge. Author a superseding ADR or a defect report. Silent divergence is the worst failure mode in this system.
- **Update `DECISIONS.md`**: confirm new significant choices are journaled; push deferred alternatives and unaddressed review concerns to the Backlog. If you removed a Backlog item, record its resolution — with a superseding ADR if it reversed a governed decision (§2.8).
- **Rewrite `HANDOFF.md` last, before you stop.** Reflect the new present: current state, in-flight work, next action, new landmines (§2.7). If you finished cleanly with nothing in flight, say so. A stale handoff is a defect (§8).

---

## 4. (reserved)

This standalone process has no cross-project change-control section; a single project governs all its contracts through its own ADRs (§1). Section number retained so downstream numbering matches the ecosystem document.

---

## 5. Conventions

Even a standalone project benefits from fixing its conventions once, in an ADR, rather than re-assuming them per file. Where the project touches geometry, physics, or other domains with silent-divergence traps, pin these explicitly when first relevant:

- **Units** and dimensional policy (SI unless an ADR states otherwise).
- **Coordinate frame and handedness**; **winding order** and normal orientation.
- **Indexing base** (0-based unless an ADR states otherwise) and index width.
- **Numerical tolerance policy** (absolute vs relative, default epsilons).
- **Error & failure semantics** (what is UB, what throws, what returns an error).

These are contracts: once fixed by an ADR, code conforms, and changing them follows the supersession path. Concurrency conventions are covered in §6.

---

## 6. Concurrency, Parallelism & GPGPU Contracts

Where the project uses shared-memory parallelism or GPU acceleration, these rules are binding. (If the project is single-threaded, this section is inert — note that fact in an ADR so the assumption is explicit.)

### 6.0 Data-race freedom is a contract, not an aspiration
- Every shared type's ADR MUST state its thread-safety contract: which operations are safe to call concurrently, under what synchronization, what the caller must guarantee.
- Default assumption absent a stated contract: **not thread-safe.**
- Prefer designs that make races *unrepresentable* (immutability, ownership transfer, partitioned access) over designs that rely on correct lock discipline.

### 6.1 Ownership across the host/device boundary (if GPU is used)
- Every GPU-resident resource has an ADR-defined owner and explicit lifetime: who allocates, who frees, who reads, who writes, when transfers happen.
- Host↔device transfer semantics (sync vs async, ordering) are stated in the ADR. A handle's *memory space* is part of its type contract — host and device handles must not be confusable. Encode the distinction in the type system, not in comments.

### 6.2 Determinism policy
- State whether a parallel/GPU result is **bitwise deterministic**, **deterministic up to a stated tolerance**, or **non-deterministic**. Floating-point reduction order is the usual culprit — name it.

### 6.3 The C++ memory model is the law
- All concurrent code conforms to the C++ memory model. No platform-specific behavior, no "volatile-as-synchronization," no benign-race rationalizations. If you reach for relaxed atomics or hand-rolled synchronization, the ordering argument goes in the ADR.

---

## 7. Quality Gates (Definition of Done)

A change is **Done** only when all hold. Verify, do not assume.

- [ ] **Orientation recorded:** governing ADRs and conventions named (§3.0).
- [ ] **Contracts honored:** no ADR violation; new contracts have Accepted ADRs (§3.1).
- [ ] **Invariants asserted:** every relied-upon invariant is `static_assert`/contract-checked, not assumed (§3.2).
- [ ] **Builds clean** under the project's required warning level, warnings-as-errors.
- [ ] **Sanitizers pass:** ASan, UBSan, and TSan (for threaded code) clean. GPU code passes the project's memory/race checker.
- [ ] **Tests pass:** unit + contract tests; concurrent tests run enough iterations/configurations to be meaningful.
- [ ] **Tests have teeth:** demonstrated, not claimed (§2.4).
- [ ] **Performance contracts met:** benchmarks within ADR-stated bounds where applicable.
- [ ] **Decisions journaled:** significant choices in `DECISIONS.md` with rationale; Backlog updated; any removal resolved correctly (§2.8).
- [ ] **Handoff current:** `HANDOFF.md` rewritten to reflect the present, or explicitly marked clean (§2.7). A stale handoff fails this gate.
- [ ] **Loop closed:** changelog/PR cites governing and new ADRs; any contradiction with an Accepted ADR resolved via supersession, never silent divergence (§3.4).

---

## 8. Anti-Patterns (Explicitly Forbidden)

Each is a defect even if the code compiles and tests pass.

1. **Re-deriving a contract you could have read.** Reading is cheaper than the divergence it prevents.
2. **Editing an Accepted ADR in place.** Supersede instead.
3. **Silent divergence:** implementing something that contradicts an Accepted ADR without raising it.
4. **Implicit conventions:** relying on units/frames/winding/indexing that aren't in an ADR.
5. **Convention leakage:** letting a third-party library's conventions spread past the boundary adapter.
6. **Unstated concurrency assumptions:** calling something concurrently because it "seemed fine."
7. **Implicit host/device synchronization or ownership.** Make it explicit and contractual.
8. **Benign-race / UB rationalization.** There are no benign data races under the C++ memory model.
9. **Invariants in comments instead of assertions.** If it matters, assert it.
10. **Public surface added without an ADR.** Every public symbol is a promise.
11. **Leaving a stale `HANDOFF.md`.** Changing project state without updating the handoff blinds the next agent.
12. **Treating `HANDOFF.md` as permanent history.** It is ephemeral and points to truth.
13. **Making a significant choice without journaling it** (§2.8).
14. **Dropping a review concern** without fixing or backlogging it.
15. **Silently deleting a Backlog item** (§2.8).

---

## 9. Glossary

- **ADR** — Architecture Decision Record: an append-only, binding record of a contractual decision.
- **Contract** — any property (signature, invariant, ownership rule, concurrency guarantee, convention) other code is entitled to rely on.
- **Boundary adapter** — the location where a third-party library's conventions are normalized to/from this project's conventions.
- **Supersede** — replace an Accepted ADR with a new one, leaving the original immutable for the record.
- **`MILESTONES.md`** — the mandatory first artifact: the ordered, living arc of milestone goals; completed milestones lock.
- **`HANDOFF.md`** — the ephemeral artifact capturing in-flight state so a fresh agent can resume; read first, written last, never permanent history.
- **`DECISIONS.md`** — the decision journal: significant choices and rationale, plus the Backlog; references ADRs rather than restating them.
- **Backlog** — the tail of `DECISIONS.md` holding roads-not-taken and deferred review concerns; items leave only via a recorded resolution, with a superseding ADR when a governed decision is reversed.
