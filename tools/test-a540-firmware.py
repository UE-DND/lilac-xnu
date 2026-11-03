#!/usr/bin/env python3
"""Check A540 firmware verifier against the actual locally acquired files and corruptions."""
from pathlib import Path
import tempfile
import a540_firmware

from lilaclib.paths import local_path
source=local_path('gpu_firmware')
original={name:(source/name).read_bytes() for name in a540_firmware.FILES}
with tempfile.TemporaryDirectory(prefix='lilac-a540-') as tmp:
    root=Path(tmp)
    for name,data in original.items():(root/name).write_bytes(data)
    result=a540_firmware.validate(root)
    assert not result['signature_verified'] and len(result['zap_segments'])==3
    for name,offset in [('a540_zap.elf',0),('a540_zap.b02',3),('a540_zap.mdt',200),('a530_pfp.fw',None)]:
        data=bytearray(original[name])
        if offset is None:data=data[:-1]
        else:data[offset]^=1
        (root/name).write_bytes(data)
        try:a540_firmware.validate(root)
        except ValueError:pass
        else:raise AssertionError(f'accepted corrupted {name}')
        (root/name).write_bytes(original[name])
print('PASS: device firmware layout and corrupt ELF, split segment, metadata and microcode size')
