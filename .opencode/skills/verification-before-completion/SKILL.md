---
name: verification-before-completion
description: "Enforce running the actual verification command before claiming work is complete. No success assertions without fresh evidence from idf.py build."
---

# Verification Before Completion

## Overview

Claiming work is complete without verification is dishonesty, not efficiency.

Core principle: **Evidence before claims, always.**

## The Iron Law

```
NO COMPLETION CLAIMS WITHOUT FRESH VERIFICATION EVIDENCE
```

If you haven't run the verification command in this message, you cannot claim it passes.

## The Gate Function

Before ANY of these statements, you MUST have fresh evidence:

- "Build passes" → requires `idf.py build` output showing "Project build complete"
- "Tests pass" → requires build output (static_asserts are compile-time)
- "Bug is fixed" → requires build + flash + observed correct behaviour
- "Task is complete" → requires ALL acceptance criteria evidenced
- "No warnings" → requires build output with no warning lines

## The Five Steps

1. **Identify** the proof command: `source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py build`
2. **Run it fresh** — not from a previous session, not from memory
3. **Read full output** — check exit code AND scan for warnings/errors
4. **Verify the claim matches** — "Project build complete" for success
5. **State the result with evidence** — quote the relevant output line

## What Counts as Evidence

| Claim | Required Evidence |
|-------|-------------------|
| "Build passes" | Terminal output: "Project build complete. To flash, run:" |
| "No warnings" | grep output showing zero warning lines |
| "static_asserts pass" | Build succeeds (they're compile-time) |
| "Flash works" | Terminal output from `idf.py flash` showing success |
| "Packets received" | Monitor output showing decoded BLE PDUs |

## What Does NOT Count

| Not Evidence | Why |
|-------------|-----|
| "I believe it will build" | Not verified |
| "Based on the code, it should work" | Not verified |
| "It compiled last time" | Stale — changes since then |
| "The logic is correct" | Correctness ≠ compilation |
| Agent confidence | Irrelevant without proof |

## Violations

If you catch yourself about to claim success without evidence:
1. STOP
2. Run the verification command
3. THEN make the claim with evidence attached

## Self-Reflection Clause

After fixing any bug or resolving any issue that required debugging, you MUST ask:
1. **Why was this bug missed?** — What review, test, or protocol gap allowed it through?
2. **What procedural safeguard would have caught it?** — What specific check, test, or verification step would have prevented it?
3. **Update the knowledge base** — Add the lesson to the relevant skill (`/home/huyang/projects/esp32/.opencode/skills/nrf24l01plus/SKILL.md` for nRF24 hardware bugs, or the appropriate learning doc in `docs/learning/`) so the same class of bug is caught earlier next time.
