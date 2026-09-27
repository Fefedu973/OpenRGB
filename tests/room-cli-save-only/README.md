# Save-only CLI regression

`ApplyOptions` must return before controller lookup, `ClearActiveProfile`, mode
selection or color validation when `DeviceOptions.hasOption` is false.
Previously a detection/profile-save invocation could reach a static Govee mode
requiring one color, then call `exit(0)` because no color was requested. The
profile save was never reached despite the apparently successful process exit.

Run in an MSVC developer shell:

```
python tests/room-cli-save-only/run.py --out <temporary-build-directory>
python tests/room-cli-save-only/run.py --out <other-directory> --without-fix
```

The runner extracts the actual production `ApplyOptions` body. Only controller,
profile-manager and ParseMode boundaries use in-memory fakes. Tests retain the
real color loop, mode-specific color-count branch, and brightness/speed logic.
No options means no controller access or mutation. Explicit color/direct-mode,
zone color, repeated last color, brightness/speed and random-color behavior are
checked separately. The pre-fix test must fail: the child exits **zero**, but the
required completion sentinel is absent. This is intentional evidence of the
original bug, not a passing test. No hardware or real app/config is accessed.
