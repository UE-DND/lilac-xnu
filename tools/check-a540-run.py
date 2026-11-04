#!/usr/bin/env python3
"""Validate a recovered A540 Mesa/desktop hardware run, not just USB success."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def validate(data, receipt, run, extended=False):
    def need(condition, why):
        if not condition:
            raise ValueError(why)
    need(receipt.get('status') == 'log_saved_fastboot_verified', 'USB recovery/Fastboot return not verified')
    need(receipt.get('sha256') == hashlib.sha256(data).hexdigest(), 'recovered log SHA-256 mismatch')
    need(receipt.get('bytes') == len(data), 'recovered log length mismatch')
    need(run.get('partition_writes') is False and run.get('firmware_updates') is False, 'RAM-only run evidence missing')
    text = data.decode('utf-8', errors='replace')
    need('PAS 205 result=0 answer=0' in text and 'left secure mode through authenticated shader' in text,
         'authenticated ZAP transition missing')
    need('IB data=540f00d CACHE_FLUSH_TS fence=1' in text, 'hardware IB/fence check missing')
    renderer = re.search(r'A540 GL_RENDERER=(\S+) GL_VERSION=([^\r\n]+)', text)
    need(renderer and renderer[1] == 'FD540', 'Freedreno A540 renderer missing')
    outcomes = re.findall(r'A540 HARDWARE RENDER: (PASS|FAIL) ([^\r\n]+)', text)
    need(outcomes and outcomes[-1][0] == 'PASS' and '4 texture quadrants matched' in outcomes[-1][1],
         'shader/texture pixel acceptance missing or failed')
    need(all(f'A540 shader frame {n}: center and background match' in text for n in range(1, 4)),
         'three distinct shader frames not verified')
    states = re.findall(r'final render state FSR=([0-9a-f]+) FAR=([0-9a-f]+) RBBM=([0-9a-f]+)', text)
    need(states and (int(states[-1][0], 16) & ~0x400) == 0, 'final SMMU state missing or faulted')
    need(states and (int(states[-1][2], 16) & ~1) == 0, 'GPU did not become idle')
    faults = re.findall(r'render faults: CP interrupt=([0-9a-f]+) hardware=([0-9a-f]+) protection=([0-9a-f]+)', text)
    need(faults and all(int(v, 16) == 0 for v in faults[-1]), 'final CP fault check missing or failed')
    stops = re.findall(r'A540 stop: sequence=(\d+) completed=(\d+) MMU=1 memory=1 PM8005=1 MX=1 CX=1', text)
    stopped = stops[-1] if stops else None
    need(stopped and int(stopped[0]) > 4 and stopped[0] == stopped[1], 'submissions incomplete or cleanup failed')
    need(not re.search(r'panic\(cpu|IB/fence failed', text), 'runtime failure in hardware log')
    expected_faults = 4 if extended else 0
    need(len(re.findall(r'submissions disabled:', text)) == expected_faults, 'unexpected runtime fault count')
    if extended:
        need('SMP WORK: PASS active=8 executed_mask=ff permission_changes=512' in text and 'data_errors=0' in text, 'SMP execution/pmap acceptance missing')
        irq = re.findall(r'SMP IRQ: cpu=(\d+) IPI=(\d+) timer=(\d+)', text)
        need({int(c) for c, i, t in irq if int(i)>0 and int(t)>0} == set(range(8)), 'not all CPUs handled IPIs and timers')
        need(all(f'psci: cpu {i} firmware confirmed OFF' in text for i in range(1,8)), 'secondary CPU shutdown not confirmed')
        need('A540 PROCESS ISOLATION: PASS same VA=4000000 peer=4000000' in text, 'cross-process alias test missing')
        for address in ('2018040','2000000','4010000'):
            need(f'A540 PROTECTION: PASS target={address} rejected and next client completed' in text, 'protection/recovery missing for '+address)
            need(re.search(r'final render state FSR=(?!400 )[0-9a-f]+ FAR='+address+r' RBBM=', text), 'hardware fault address missing for '+address)
        need('A540 PROTECTION: PASS target=831 rejected and next client completed' in text, 'IB privilege escalation was not rejected')
        need('protection=50020c4' in text, 'CP did not report the protected CP_CNTL write')
        need(len(re.findall(r'quarantined context=\d+ canceled_through=\d+ reset confirmed', text)) == expected_faults, 'fault containment/reset count differs')
        need('clock: idle=180000000 Hz' in text and 'clock: active=257000000 Hz' in text, 'clock transitions not observed')
        need(text.count('PM: suspended sequence=') >= 3 and text.count('PM: resumed sequence=') >= 3, 'repeated suspend/resume missing')
        images = re.findall(r'authenticated image 13, dedicated memory=([0-9a-f]+)', text)
        need(len(images)>=4 and len(set(images))==1, 'ZAP memory was not reused across resumes')
    frames = [(int(n), int(ms)) for n, ms in re.findall(r'MiniIOMobileFramebuffer: (\d+) frames in (\d+) ms', text)]
    frames = [(n, ms) for n, ms in frames if n >= 100 and ms > 0]
    need(len(frames) >= 2, 'sustained desktop presentation not observed')
    return {'status': 'passed', 'renderer': renderer[1], 'gl_version': renderer[2],
            'submissions_retired': int(stopped[1]),
            'submissions_completed': int(stopped[1]) - expected_faults,
            'expected_faulted_submissions': expected_faults, 'desktop_intervals': len(frames),
            'desktop_fps_min': round(min(n*1000/ms for n, ms in frames), 2),
            'desktop_fps_max': round(max(n*1000/ms for n, ms in frames), 2),
            'log_sha256': receipt['sha256'], 'image_sha256': run.get('image_sha256'),
            'scope': 'SMP, GPU isolation/fault recovery, clock changes and suspend/resume' if extended else 'shader/texture pixels, Mesa submissions, desktop presentation and shutdown'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--extended', action='store_true', help='require SMP, isolation, 4 deliberate faults, clocks and resume evidence')
    args = parser.parse_args()
    result = validate((args.run/'kernel.log').read_bytes(), json.loads((args.run/'receipt.json').read_text()),
                      json.loads((args.run/'result.json').read_text()), args.extended)
    text = json.dumps(result, indent=2, ensure_ascii=False)+'\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
