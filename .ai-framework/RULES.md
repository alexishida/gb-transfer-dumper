# AI Rules

This file defines the primary AI guidelines for this project.
These instructions must guide analysis, implementation, visual changes, and
decision-making.

# General Rules

- Preserve existing behavior unless the requested change explicitly requires
  different behavior.
- Investigate existing comments, changelog entries, tests, and compatibility
  workarounds before replacing or removing code.
- Treat regressions already documented by the project as mandatory test cases.
- State any validation that could not be performed, especially tests requiring
  real Nintendo 64 hardware.

# Code Rules

- Write clear, organized, and maintainable code.
- Respect the project's architecture, conventions, and directory structure.
- Keep hardware-specific behavior documented next to the code that implements
  it.
- Do not introduce dependencies, abstractions, or structural changes without a
  concrete project requirement.

# Transfer Pak Compatibility Rules

- A successful `trpak_init()` is the strict readiness handshake. During an
  active transfer, some Transfer Pak revisions report `READY` and `RESETTING`
  inconsistently even while block I/O remains functional.
- During active ROM dumps, save backups, and save restores, do not make
  `TRPAK_STATUS_READY` or `TRPAK_STATUS_IS_RESETTING` authoritative failure
  conditions. Block I/O is authoritative after successful initialization.
- Continue treating Joybus I/O failures, `TRPAK_STATUS_REMOVED`, and a cleared
  `TRPAK_STATUS_POWERED` bit as authoritative failures.
- Preserve `TRPAK_STATUS_WAS_RESET`. Reset recovery must still reselect the
  affected mapper bank, re-enable cartridge RAM when required, and retry the
  affected block.
- Treat consecutive high readings of `TRPAK_STATUS_WAS_RESET` as one reset
  event. Some hardware keeps this bit high instead of clearing it on read; a
  low reading must re-arm detection of the next reset.
- Never remove the active-transfer status compatibility logic without explicit
  evidence that all supported Transfer Pak revisions behave correctly and
  validation on real hardware.

# Version and Dependency Change Rules

- Before changing the application version, `libtrpak` revision, `libdragon`
  revision, or transfer implementation, review the full dependency diff and
  all previous Transfer Pak compatibility fixes in code and `CHANGELOG.md`.
- A version or dependency update must not silently override local hardware
  compatibility behavior.
- Every transfer-related version change must test this regression case: with
  power set and no removal reported, an active transfer must continue when
  `READY` remains clear or `RESETTING` remains set.
- Every transfer-related version change must also test cartridge removal,
  power loss, Joybus failure, normal `WAS_RESET` recovery, and a sticky
  `WAS_RESET` bit that remains high across consecutive status reads.
- Do not release or bump the version when these regression checks fail. If real
  hardware testing is unavailable, record that limitation and keep the change
  unreleased until a responsible maintainer validates it.

# Guard Rails

- Do not run commands directly in production. When needed, provide the command
  and instruct the responsible maintainer to run it manually.
- Do not make destructive or irreversible changes without explicit
  confirmation and a clear explanation of the expected impact.
- Do not introduce dependencies, abstractions, styles, or structures based only
  on personal preference.
- Do not ignore security, performance, usability, maintainability, or visual
  consistency impacts.
