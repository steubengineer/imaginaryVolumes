# ADR-0004: Vulkan Binding & Object-Ownership Model

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
M2 brings Vulkan up. ADR-0001 pre-authorized the Vulkan dependency
(`find_package(Vulkan)`, required from M2). ADR-0003 fixes our error model:
recoverable failures return `iv::Result<T>`, no exceptions are used as a control
channel, and `IV_ASSERT` handles contract violations. We must decide how to bind
to Vulkan and how to own Vulkan objects' lifetimes. Probe (2026-06-18): loader
1.4.313; `vulkan.h`, `vulkan.hpp`, and `vulkan_raii.hpp` all present; the only
enumerable device is `llvmpipe` (software). The maintainer chose **Vulkan-Hpp in
no-exceptions mode** (D-0011).

## Decision
**Bind via Vulkan-Hpp (`<vulkan/vulkan.hpp>`) compiled with
`VULKAN_HPP_NO_EXCEPTIONS`.** Vulkan-Hpp entry points then return `vk::Result` /
`vk::ResultValue<T>` instead of throwing. All macro configuration is centralized
in one project header, `include/iv/vk/vulkan.hpp`, which sets the macros **before**
including `<vulkan/vulkan.hpp>`; every TU that touches Vulkan includes that header
(ODR-uniform). `VULKAN_HPP_ASSERT_ON_RESULT` is defined to a no-op — we check
every result explicitly.

**Dispatch:** link the Vulkan loader (`Vulkan::Vulkan`) and use Vulkan-Hpp's
default dispatch. M2 uses only core functionality (≤ Vulkan 1.3) exported by the
loader, so the dynamic dispatcher is not needed; adopting it later (if an
extension requires it) is a contained, superseding change.

**Result mapping (boundary adapter, §9).** A single helper converts `vk::Result`
→ `iv::Status`/`iv::Result<T>`, mapping Vulkan codes to `iv::Errc`:
- `eErrorOutOfHostMemory`, `eErrorOutOfDeviceMemory` → `allocation_failed`
- `eErrorDeviceLost`, `eErrorInitializationFailed` → `device_unavailable`
- `eErrorExtensionNotPresent`, `eErrorFeatureNotPresent`,
  `eErrorLayerNotPresent`, `eErrorFormatNotSupported` → `unsupported_configuration`
- any other non-success → `internal`
`vk::Result`/`VkResult` never appears in a consumer-facing signature; the mapping
is the only place Vulkan codes cross into our model.

**Ownership model.** Vulkan objects are owned by **move-only RAII wrapper types we
define** (we do *not* use `vk::raii`, whose constructors signal failure via
exceptions, conflicting with ADR-0003). Each wrapper:
- holds the `vk::` handle plus whatever parent/dispatch it needs to destroy itself;
- is **single-owner**: copy is deleted; move transfers ownership and leaves the
  source null;
- destroys its object **exactly once** in its destructor via the correct
  `destroy`/`free`; a null (default-constructed or moved-from) wrapper destroys to
  a **no-op**;
- is created by a **factory function returning `iv::Result<Wrapper>`** (no throwing
  constructors);
- exposes the underlying handle by `const` accessor for use in Vulkan calls, never
  duplicating ownership.
Destruction order is enforced structurally: a parent wrapper (e.g. Device) is
declared before — and so destroyed after — the children created from it.

## Contract Specification
- Every Vulkan-touching TU includes `iv/vk/vulkan.hpp`; `VULKAN_HPP_NO_EXCEPTIONS`
  is in effect uniformly. No `try`/`catch` around Vulkan.
- No Vulkan result is ignored: each is mapped via the helper or `IV_ASSERT`ed.
- Wrappers are **move-only** (`static_assert(!std::is_copy_constructible_v<W> &&
  std::is_move_constructible_v<W>)`), null-safe on destruction, single-destroy.
- **Invariant (assertable):** a moved-from wrapper holds a null handle and its
  destructor issues no `vkDestroy*`/`vkFree*`.
- No `vk::Result`/`VkResult` in any consumer-facing signature (boundary adapter,
  §8.5). The `VkResult→Errc` mapping table above is fixed here.

## Consequences
- Type-safety and exception-free errors, matching ADR-0003.
- We hand-roll RAII (some boilerplate) but gain explicit, auditable single-owner
  lifetimes — exactly what the ownership contract needs, and double-frees/leaks
  surface under the validation layer (ADR-0005) and ASan.
- Default dispatch keeps M2 simple; the dynamic dispatcher remains a contained
  future option.
- Wrappers expose raw handles read-only; discipline is required not to store them.

## Alternatives Considered
- **`vk::raii`:** rejected — constructor-failure-via-exceptions conflicts with
  ADR-0003; would force exception containment throughout.
- **C API (`vulkan.h`) + own RAII:** declined by the maintainer — more boilerplate,
  weaker type safety, manual `sType`/destroy-ordering footguns.
- **Vulkan-Hpp *with* exceptions:** rejected — violates ADR-0003.
- **Dynamic dispatcher from the start:** deferred — unneeded for core-only M2;
  adds global-state initialization.

## Verification
- `static_assert`s pin move-only/non-copyable wrappers.
- A factory success path returns a valid wrapper; a forced-failure path (e.g.
  requesting an unavailable feature/extension via ADR-0005) returns the **mapped
  `Errc`** (teeth: alter the mapping → wrong `Errc` → red).
- A move test asserts the moved-from wrapper is null and destroying it is a no-op
  (teeth: a double-free would trip the validation layer / ASan).
- The M2 device bring-up runs validation-clean (no leaked/double-freed objects at
  teardown — the validation layer reports such defects).
