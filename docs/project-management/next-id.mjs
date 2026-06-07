#!/usr/bin/env node

/**
 * Generate the next ticket, epic, clarification, ADR, advisory, design, or chore number(s).
 *
 * Adapted from the OAC (international-space-bar) project management system
 * for the ESP32 nRF24L01+ embedded C++ project.
 *
 * Usage:
 *   node next-id.mjs ticket              # next ticket id
 *   node next-id.mjs ticket 5            # next 5 ticket ids
 *   node next-id.mjs epic                # next epic id
 *   node next-id.mjs epic 3              # next 3 epic ids
 *   node next-id.mjs clarification       # next clarification id
 *   node next-id.mjs adr                 # next ADR id
 *   node next-id.mjs advisory            # next advisory id
 *   node next-id.mjs design              # next design id
 *   node next-id.mjs chore               # next chore id
 *   node next-id.mjs ticket --dry-run    # preview without updating counters
 *
 * Output (JSON, one object per run — easy for LLMs to parse):
 *   { "kind": "ticket", "ids": ["nrf-0016"], "dryRun": false }
 */

import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const COUNTERS_PATH = resolve(__dirname, "counters.json");

const KIND_CONFIG = {
    ticket:        { key: "lastTicket",        prefix: "nrf",        width: 4 },
    epic:          { key: "lastEpic",           prefix: "nrf-epic",    width: 3 },
    clarification: { key: "lastClarification",   prefix: "nrf-clar",    width: 4 },
    adr:           { key: "lastAdr",             prefix: "nrf-adr",     width: 4 },
    advisory:      { key: "lastAdvisory",        prefix: "nrf-adv",     width: 4 },
    design:        { key: "lastDesign",          prefix: "nrf-design",  width: 4 },
    chore:         { key: "lastChore",           prefix: "nrf-chore",   width: 4 },
};

const DEFAULT_COUNTERS = {
    lastTicket: 0,
    lastEpic: 0,
    lastClarification: 0,
    lastAdr: 0,
    lastAdvisory: 0,
    lastDesign: 0,
    lastChore: 0,
};

// ── Argument parsing ────────────────────────────────────────────────

function parseArgs(argv) {
    const args = argv.slice(2);
    const dryRun = args.includes("--dry-run");
    const positional = args.filter((a) => a !== "--dry-run");

    const kind = positional[0];
    if (!kind || !(kind in KIND_CONFIG)) {
        console.error(
            "Usage: node next-id.mjs <ticket|epic|clarification|adr|advisory|design|chore> [count] [--dry-run]",
        );
        process.exit(1);
    }

    const count = positional[1] ? Number.parseInt(positional[1], 10) : 1;
    if (!Number.isFinite(count) || count < 1) {
        console.error("Count must be a positive integer.");
        process.exit(1);
    }

    return { kind, count, dryRun };
}

// ── Counter I/O ─────────────────────────────────────────────────────

function loadCounters() {
    if (!existsSync(COUNTERS_PATH)) {
        saveCounters(DEFAULT_COUNTERS);
        return { ...DEFAULT_COUNTERS };
    }
    const stored = JSON.parse(readFileSync(COUNTERS_PATH, "utf-8"));
    // Merge with defaults so new keys are added automatically
    return { ...DEFAULT_COUNTERS, ...stored };
}

function saveCounters(counters) {
    writeFileSync(COUNTERS_PATH, JSON.stringify(counters, null, 2) + "\n");
}

// ── ID generation ───────────────────────────────────────────────────

function formatId(prefix, number, width) {
    return `${prefix}-${String(number).padStart(width, "0")}`;
}

function generateIds(counters, kind, count) {
    const { key, prefix, width } = KIND_CONFIG[kind];
    const ids = [];
    let last = counters[key];

    for (let i = 0; i < count; i++) {
        last += 1;
        ids.push(formatId(prefix, last, width));
    }

    return { ids, updatedLast: last, counterKey: key };
}

// ── Main ────────────────────────────────────────────────────────────

function run() {
    const { kind, count, dryRun } = parseArgs(process.argv);
    const counters = loadCounters();
    const { ids, updatedLast, counterKey } = generateIds(counters, kind, count);

    if (!dryRun) {
        counters[counterKey] = updatedLast;
        saveCounters(counters);
    }

    const result = { kind, ids, dryRun };
    console.log(JSON.stringify(result, null, 2));
}

run();