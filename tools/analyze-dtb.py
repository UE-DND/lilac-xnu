#!/usr/bin/env python3
"""Summarize a DTB without running device code or treating addresses as ported."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dtb', type=Path)
    args = parser.parse_args()
    from lilaclib.paths import HAKIOS
    source = HAKIOS / 'tools/soc-args.py'
    spec = importlib.util.spec_from_file_location('soc_args', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    data = args.dtb.read_bytes()
    if data[:4] != bytes.fromhex('d00dfeed'):
        parser.error('not a flattened device tree')
    nodes = module.nodes_of(data)
    keywords = ('interrupt-controller', 'psci', 'memory', 'reserved-memory', 'kgsl', 'gpu', 'smmu', 'ufshc', 'spmi', 'rpm', 'mdss', 'panel', 'clearpad', 'synaptics', 'touch')
    report = {'sha256': hashlib.sha256(data).hexdigest(), 'nodes': {}}
    for name, props in nodes.items():
        compat = props.get('compatible', b'').replace(b'\0', b' ').decode(errors='replace')
        if name == '' or any(word in name.lower() or word in compat.lower() for word in keywords):
            selected = {}
            for key in ('model', 'compatible', 'reg', 'method', 'qcom,msm-id', 'qcom,board-id', 'interrupts', '#address-cells', '#size-cells', 'status'):
                if key in props:
                    value = props[key]
                    selected[key] = value.rstrip(b'\0').decode(errors='replace') if key in ('model', 'compatible', 'method', 'status') else value.hex()
            report['nodes'][name] = selected
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
