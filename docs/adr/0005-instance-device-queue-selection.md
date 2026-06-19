# ADR-0005: Instance, Physical-Device & Queue Selection

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
M2 stands up Vulkan headlessly (no surface/swapchain — those arrive in M5). We
must create an instance (with validation in Debug), choose a physical device, a
queue family, and a logical device. The only device on the dev host is `llvmpipe`
(software, `PHYSICAL_DEVICE_TYPE_CPU`), so selection **must accept a software
device** and must not require a discrete GPU. Binding and error mapping per
ADR-0004; concurrency per ADR-0007.

## Decision
**API version:** target a **Vulkan 1.3** baseline (`apiVersion =
VK_API_VERSION_1_3`) — gives synchronization2 and dynamic rendering in core for
later milestones. Devices must report `apiVersion ≥ 1.3` (llvmpipe reports 1.4).

**Instance:** `vk::ApplicationInfo{ pApplicationName="imaginaryVolumes",
apiVersion=1.3 }`. In **Debug** builds, enable layer `VK_LAYER_KHRONOS_validation`
and instance extension `VK_EXT_debug_utils`, **iff** they are available (a missing
validation layer is logged and skipped, not fatal — it is a dev aid, not a
contract). In **Release**, no layers, no debug messenger.

**Debug messenger:** install a `vk::DebugUtilsMessengerEXT` whose callback records
messages. The renderer exposes a query for **whether any WARNING/ERROR-severity
message has been emitted** since creation, so tests can assert validation
cleanliness. (Captured, not aborting, so tests can inspect.)

**Physical-device selection:** enumerate all devices; **filter** to those with
`apiVersion ≥ 1.3` and at least one **graphics-capable** queue family (graphics
implies transfer and permits `vkCmdClearColorImage`). **Rank** survivors by type:
discrete > integrated > virtual > CPU > other; tie-break by largest device-local
heap. Pick the top. Environment override **`IV_VULKAN_DEVICE_INDEX`** forces a
specific enumerated index (CI / multi-GPU). If no device qualifies (or the
override is out of range), return `iv::Errc::device_unavailable`.

**Queue & logical device:** select the first graphics-capable queue family; create
the logical device with **one** queue from it (priority 1.0). No special features
and **no device extensions** for M2 (swapchain is M5). Record the chosen device
and queue-family index.

**Determinism:** given the same enumerated set and the same `IV_VULKAN_DEVICE_INDEX`,
selection is deterministic.

## Contract Specification
- Instance `apiVersion ≥ 1.3`; devices filtered to `apiVersion ≥ 1.3` **and** a
  graphics-capable queue family.
- Ranking: `discrete > integrated > virtual > cpu > other`, tie-break by largest
  device-local heap; `IV_VULKAN_DEVICE_INDEX` overrides selection.
- No qualifying device, or out-of-range override ⇒ `Errc::device_unavailable`.
- Debug: validation layer + `VK_EXT_debug_utils` enabled when present; a query
  reports whether any WARNING/ERROR validation message occurred. Release: neither
  is enabled.
- Exactly one graphics queue is created in M2.
- All Vulkan failures map to `Errc` per ADR-0004.

## Consequences
- Runs on software (required here) and on real GPUs (preferring discrete).
- Capturing validation messages makes "validation-clean" a **testable** gate.
- One queue keeps M2 simple; a dedicated transfer/compute queue is a later,
  ADR-bearing addition if needed.

## Alternatives Considered
- **Require a discrete GPU:** rejected — none exists here, and the library should
  run anywhere a conformant device (incl. software) is present.
- **Fail Debug init if the validation layer is absent:** rejected — would break on
  machines without the layer; validation is a dev aid, so it is best-effort.
- **Separate transfer queue for readback:** deferred — unneeded for M2's single
  clear+copy.
- **Target Vulkan 1.0/1.1 baseline:** rejected — 1.3 core (sync2, dynamic
  rendering) simplifies M4; llvmpipe supports it.

## Verification
- On the dev host, instance+device creation succeeds and selects `llvmpipe`; the
  chosen queue family is graphics-capable (teeth: assert the family's flags
  include graphics — a wrong family index fails).
- `IV_VULKAN_DEVICE_INDEX` set out of range ⇒ `Errc::device_unavailable` (teeth:
  forced failure returns the correct code; breaking the bounds check → red).
- The debug messenger reports **zero** WARNING/ERROR messages across creation and
  teardown (teeth: deliberately misuse the API — e.g. destroy an object out of
  order — and confirm the messenger's count goes nonzero; then restore).
