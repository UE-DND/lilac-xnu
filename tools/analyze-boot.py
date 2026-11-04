#!/usr/bin/env python3
"""Extract boot stages and migration lifetimes without treating a frame as desktop readiness."""
import argparse
import collections
import datetime
import hashlib
import json
from pathlib import Path
import re

WALL = re.compile(r'com\.apple\.xpc\.launchd\|(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)')
JOB = re.compile(r'\((?:[^)]+/)?(com\.apple\.[\w.]+) \[(\d+)\]\)')
CHECKPOINT = re.compile(r'FAST BOOT (\S+) uptime_ms=(\d+)')
STORAGE = re.compile(r'FAST BOOT (\S+) UFS (.*)')
COUNTER = re.compile(r'^\[\s*(\d+\.\d+)\]:')


def analyze(text):
    stages, storage, events, migrations, active = [], [], [], [], {}
    lookups = collections.Counter()
    repeated = []
    first_wall = None
    first_swap = None
    for number, line in enumerate(text.splitlines(), 1):
        match = CHECKPOINT.search(line)
        if match:
            stages.append({'stage': match[1], 'uptime_ms': int(match[2]), 'line': number})
        match = STORAGE.search(line)
        if match:
            values = dict(re.findall(r'(\w+)=(\w+)', match[2]))
            sample = {'stage': match[1], 'line': number, 'counters': values}
            frequency = int(values.get('tick_hz', '0'))
            if frequency:
                sample['read_seconds'] = round(int(values.get('read_ticks', '0'))/frequency, 6)
            storage.append(sample)
        match = WALL.search(line)
        if match:
            wall = datetime.datetime.fromisoformat(match[1])
            if first_wall is None:
                first_wall = wall
            job = JOB.search(line)
            if job:
                label, pid = job[1], int(job[2])
                if 'Successfully spawned ' in line:
                    event = {'label': label, 'pid': pid, 'wall_time': wall.isoformat(),
                             'seconds_from_launchd_log': round((wall-first_wall).total_seconds(), 6),
                             'line': number}
                    if label in ('com.apple.SpringBoard', 'com.apple.backboardd', 'com.apple.datamigrator'):
                        events.append(event)
                    if label == 'com.apple.migrationpluginwrapper':
                        span = {**event, 'end_line': None, 'duration_seconds': None, 'exit': None}
                        migrations.append(span)
                        active[pid] = (wall, span)
                if 'exited due to ' in line and pid in active and label == 'com.apple.migrationpluginwrapper':
                    start, span = active.pop(pid)
                    span.update(end_line=number, duration_seconds=round((wall-start).total_seconds(), 6),
                                exit=line.split('exited due to ', 1)[1])
        match = re.search(r'failed lookup: name = ([^,]+),.*?requestor = ([^,]+)', line)
        if match:
            lookups[(match[1], match[2])] += 1
        match = re.search(r'Last log repeated (\d+) times', line)
        if match:
            repeated.append({'line': number, 'count': int(match[1])})
        if first_swap is None and 'MiniIOMobileFramebuffer: swap #' in line:
            counter = COUNTER.search(line)
            first_swap = {'line': number, 'counter_seconds': float(counter[1]) if counter else None}
    return {
        'stages': stages, 'storage': storage, 'process_starts': events,
        'migration_wrappers': migrations, 'first_swap': first_swap,
        'failed_lookups': [{'service': key[0], 'requestor': key[1], 'observed_lines': count}
                           for key, count in lookups.most_common()],
        'collapsed_repeats': repeated,
        'limits': ['Spawn and swap events do not establish an interactive desktop.',
                   'Unclosed migration lifetimes are incomplete, not zero or successful.',
                   'Kernel counter timestamps can include firmware time; do not read them as boot uptime.',
                   'Launchd wall-clock differences are relative to its first log and depend on a stable clock.',
                   'Failed lookup counts are observed lines; collapsed repeats are not attributed to a service.',
                   'These logs do not identify the migrator plugin or CPU/IO usage after launchd exec.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--json', type=Path, help='save full evidence including one-based log lines')
    args = parser.parse_args()
    data = args.log.read_bytes()
    report = analyze(data.decode(errors='replace'))
    report.update(log=str(args.log.resolve()), sha256=hashlib.sha256(data).hexdigest())
    for stage in report['stages']:
        print(f"{stage['stage']}: {stage['uptime_ms']/1000:.3f}s (line {stage['line']})")
    for span in report['migration_wrappers']:
        duration = 'incomplete' if span['duration_seconds'] is None else f"{span['duration_seconds']:.3f}s"
        print(f"migration wrapper PID {span['pid']}: {duration}; lines {span['line']}..{span['end_line']}")
    print(f"{len(report['failed_lookups'])} service/requestor pairs with failed lookups; desktop readiness unverified")
    if args.json:
        args.json.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')


if __name__ == '__main__':
    main()
