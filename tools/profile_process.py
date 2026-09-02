#!/usr/bin/env python3
"""Measure CPU and RSS for one already-running Linux process."""

import argparse
import os
import time


def process_ticks(pid):
    with open(f"/proc/{pid}/stat", encoding="ascii") as stream:
        fields = stream.read().rsplit(")", 1)[1].split()
    return int(fields[11]) + int(fields[12])


def rss_kib(pid):
    with open(f"/proc/{pid}/status", encoding="ascii") as stream:
        for line in stream:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    raise RuntimeError("VmRSS is unavailable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()
    if args.pid <= 0 or args.duration <= 0 or args.interval <= 0:
        parser.error("pid, duration, and interval must be positive")

    clock_ticks = os.sysconf("SC_CLK_TCK")
    started = time.monotonic()
    previous_time = started
    previous_ticks = process_ticks(args.pid)
    rss_values = [rss_kib(args.pid)]
    cpu_values = []

    while True:
        remaining = args.duration - (time.monotonic() - started)
        if remaining <= 0:
            break
        time.sleep(min(args.interval, remaining))
        now = time.monotonic()
        ticks = process_ticks(args.pid)
        cpu_values.append(100.0 * (ticks - previous_ticks) / clock_ticks / (now - previous_time))
        rss_values.append(rss_kib(args.pid))
        previous_time, previous_ticks = now, ticks

    elapsed = time.monotonic() - started
    print(f"Duration: {elapsed:.2f} s")
    print(f"Average CPU: {sum(cpu_values) / len(cpu_values):.2f} %")
    print(f"Peak CPU: {max(cpu_values):.2f} %")
    print(f"Starting RSS: {rss_values[0]} KiB")
    print(f"Average RSS: {sum(rss_values) / len(rss_values):.0f} KiB")
    print(f"Peak RSS: {max(rss_values)} KiB")
    print(f"Ending RSS: {rss_values[-1]} KiB")
    print(f"RSS growth: {rss_values[-1] - rss_values[0]} KiB")


if __name__ == "__main__":
    main()
