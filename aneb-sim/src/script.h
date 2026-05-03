/*
 * script.h — JSON-Lines scenario player.
 *
 * A scenario file is a `.jsonl` document where each line is a normal
 * engine command JSON object plus a leading `"at_ms"` field that
 * specifies *when* (in milliseconds since the script started) to
 * apply that command:
 *
 *     {"at_ms":  500, "c": "load",        "chip": "ecu1", "path": "..."}
 *     {"at_ms": 1500, "c": "force_busoff","chip": "ecu2"}
 *     {"at_ms": 4000, "c": "can_recover", "chip": "ecu2"}
 *
 * `script_run_async` opens the file, parses it, and spawns a thread
 * that sleeps to each `at_ms` then feeds the parsed command into
 * the same `cmd_queue_push` path the stdin reader uses — so a
 * scenario is indistinguishable from a human typing the same
 * commands at the same wallclock times.
 *
 * Lines beginning with `#` or `//` are treated as comments and skipped.
 * Empty lines are skipped.  Lines with `at_ms` < the previous line's
 * are still respected (the thread sleeps to the absolute time, never
 * fires retroactively); the only practical effect is the script runs
 * in line order with the gaps the author put in.
 *
 * One scenario at a time: a second `script_run_async` invocation while
 * a first is still running spawns its own thread independently.  No
 * cross-coordination is attempted.  Calls to other commands (from
 * stdin, from a UI button, etc.) interleave with the scenario freely
 * — the queue is the single point of serialization.
 */
#ifndef ANEB_SCRIPT_H
#define ANEB_SCRIPT_H

/*
 * Spawn a detached thread that replays `path`.  Returns 0 on success
 * (file opened and thread created); negative on early failure.  Errors
 * encountered while replaying (parse errors per line, queue-full) are
 * emitted as `proto_emit_log` warnings.
 */
int script_run_async(const char *path);

#endif /* ANEB_SCRIPT_H */
