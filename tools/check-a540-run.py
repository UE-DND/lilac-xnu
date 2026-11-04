#!/usr/bin/env python3
"""Validate a recovered A540 Mesa/desktop hardware run, not just USB success."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def validate(data, receipt, run):
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
    need('render faults: CP interrupt=0 hardware=0 protection=0' in text, 'final CP fault check missing or failed')
    stopped = re.search(r'A540 stop: sequence=(\d+) completed=(\d+) MMU=1 memory=1 PM8005=1 MX=1 CX=1', text)
    need(stopped and int(stopped[1]) > 4 and stopped[1] == stopped[2], 'submissions incomplete or cleanup failed')
    need(not re.search(r'panic\(cpu|submissions disabled:|IB/fence failed', text), 'runtime failure in hardware log')
    frames = [(int(n), int(ms)) for n, ms in re.findall(r'MiniIOMobileFramebuffer: (\d+) frames in (\d+) ms', text)]
    frames = [(n, ms) for n, ms in frames if n >= 100 and ms > 0]
    need(len(frames) >= 2, 'sustained desktop presentation not observed')
    return {'status': 'passed', 'renderer': renderer[1], 'gl_version': renderer[2],
            'submissions_completed': int(stopped[2]), 'desktop_intervals': len(frames),
            'desktop_fps_min': round(min(n*1000/ms for n, ms in frames), 2),
            'desktop_fps_max': round(max(n*1000/ms for n, ms in frames), 2),
            'log_sha256': receipt['sha256'], 'image_sha256': run.get('image_sha256'),
            'scope': 'shader/texture pixels, Mesa submissions, desktop presentation and shutdown; fixed clock'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = validate((args.run/'kernel.log').read_bytes(), json.loads((args.run/'receipt.json').read_text()),
                      json.loads((args.run/'result.json').read_text()))
    text = json.dumps(result, indent=2, ensure_ascii=False)+'\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
