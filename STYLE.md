# Source style

This pass changes presentation and internal names only. It does not claim that the code was written without assistance or that an authorship detector will classify it a particular way.

## What the research can tell us

AI-code studies examine comments, identifier choices, formatting, code metrics and syntax-tree patterns. These are statistical features, not proof of authorship. Suh et al.'s ICSE 2025 study found poor generalization across languages and code sources; normalizing variable names had little effect on their best model, and removing comments had a statistically insignificant effect. That is a reason to improve readability rather than promise detector evasion.

Source: [An Empirical Study on Automatically Detecting AI-Generated Source Code](https://arxiv.org/html/2411.04299v1). These results describe the studied models and datasets, not a detector test of THE VAULT or every current detector.

The practical guidance here also follows the emphasis on useful names, meaningful comments and consistency in the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html#Comments). THE VAULT keeps its own existing brace and naming conventions; it does not adopt Google's entire style or language-version policy.

## What changed in THE VAULT

| Previous style issue | Change |
|---|---|
| Repeated full-width banners and numbered tutorial headings | Short sentence-case section labels |
| Long file introductions repeating the README | Brief responsibility and constraint summaries |
| Comments narrating ordinary copies, pointers or nearby statements | Kept explanations of purpose, units, ownership and failure handling |
| Mixed indentation and tightly packed host mocks | Consistent four-space indentation, braces on separate lines and expanded function bodies |
| Long or ambiguous local names | Shorter names where context is clear; descriptive counters where it is not |

Examples of the identifier changes:

| Before | After | Reason |
|---|---|---|
| `currentTimeMs` or `now` in firmware | `nowMs` | Consistent clock name with units |
| `horizontalPosition` / `verticalPosition` | `xPosition` / `yPosition` | Joystick axes are already clear from context |
| `horizontalDirection` / `verticalDirection` | `xDirection` / `yDirection` | Matches the axis positions |
| `measuredPulseDurationUs` | `pulseWidthUs` | Concise ultrasonic pulse terminology |
| `unfilteredDistanceCm` | `rawDistanceCm` | Distinguishes the input from the filtered result |
| `n` / `k` in MATLAB capture | `nodeCount` / `nodeIndex` | Makes two-node iteration explicit |
| `fid` in MATLAB replay/tests | `fileId` | Clarifies the file handle |
| `time` in MATLAB replay | `timeSeconds` | Makes plot units explicit |

Concurrency, sample age, voltage-divider, packet-format, calibration, warning-hold and shutter-visibility explanations remain. Function names, state fields, packet layout, constants, numerical operations, text output, saved keys, filenames and hardware assignments retain their existing behavior.

## Keeping the style consistent

`.clang-format` is the source of truth for C++ layout. It uses clang-format 18.1.8, four spaces, Allman braces and a 100-column target. It preserves include order and string literals, so some long diagnostic strings exceed that target. It does not compress statement blocks into one line.

Install the tools from `requirements-dev.txt`, then format the C++ files from the repository root:

```bash
clang-format -i Receiver/src/*.cpp Receiver/src/*.h Sender/src/*.cpp Sender/src/*.h tests/system_test.cpp tests/support/*.h
```

GitHub Actions checks this formatting as well as the existing build/test/lint steps. MATLAB uses conventional spacing and blank lines between logical blocks. Comments should explain a constraint or decision; keep teaching material in the learning guide when it would obscure the implementation.

## Behavior-preservation checks from the style pass

- Compared all 23 code files with the pre-style THE VAULT snapshot. Executable tokens match after the explicitly recorded local/parameter renames; MATLAB statement boundaries and matrix separators were retained, and Python's AST was unchanged.
- Independently reviewed the rename scopes; no public fields were renamed and no local-name collision was found.
- Both ESP32 builds passed. All allocated ELF sections, including executable instructions and initialized data, have matching bytes, addresses and sizes before/after this pass. Debug information and complete image metadata are not claimed to be identical.
- The host suite still passes 9,557 assertions. All five MATLAB scripts pass static lint; all 17 C++ files pass the formatter check.
- `platformio.ini`, the example recording, pins, preferences, protocol and sensitivity values were preserved.

`style-verification.json` records source hashes and allowed renames. `style-elf-comparison.json` records the compiled section comparison. These checks support a style-only change for the tested configuration; they do not replace hardware tests or MATLAB runtime execution. Failed host-test diagnostics may now show different source line numbers because formatting moved the checks.

## Project name update

The subsequent project rename to THE VAULT changes LCD headings, the serial startup message, MATLAB window titles, the host-test temporary-folder prefix and documentation/package names only. The token and ELF reports above are retained as historical evidence for the style pass before this rename; their hashes and byte-identical result are not a comparison of the renamed binaries. Current source hashes are in `source-manifest.json`.
