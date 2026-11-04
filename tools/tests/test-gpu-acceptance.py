import hashlib
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('gpu_check', Path(__file__).resolve().parents[1]/'check-a540-run.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class AcceptanceTests(unittest.TestCase):
    log = '''PAS 205 result=0 answer=0
left secure mode through authenticated shader
IB data=540f00d CACHE_FLUSH_TS fence=1
A540 GL_RENDERER=FD540 GL_VERSION=OpenGL ES 3.1 Mesa 25.0.7
A540 shader frame 1: center and background match
A540 shader frame 2: center and background match
A540 shader frame 3: center and background match
A540 HARDWARE RENDER: PASS 4 texture quadrants matched
final render state FSR=400 FAR=0 RBBM=1
render faults: CP interrupt=0 hardware=0 protection=0
MiniIOMobileFramebuffer: 145 frames in 5000 ms
MiniIOMobileFramebuffer: 146 frames in 5010 ms
A540 stop: sequence=100 completed=100 MMU=1 memory=1 PM8005=1 MX=1 CX=1
'''

    def validate(self, text):
        data = text.encode()
        receipt = {'status': 'log_saved_fastboot_verified', 'bytes': len(data),
                   'sha256': hashlib.sha256(data).hexdigest()}
        return check.validate(data, receipt, {'partition_writes': False, 'firmware_updates': False})

    def test_full_evidence(self):
        self.assertEqual(self.validate(self.log)['submissions_completed'], 100)

    def test_cp_only_and_software_are_not_gpu_acceptance(self):
        for old, new in [('FD540', 'softpipe'), ('4 texture quadrants matched', 'CP smoke passed'),
                         ('frame 3:', 'frame missing:'), ('PAS 205 result=0', 'PAS 205 result=-68'),
                         ('completed=100', 'completed=99'), ('MX=1', 'MX=0'), ('FSR=400', 'FSR=402'),
                         ('RBBM=1', 'RBBM=101'), ('hardware=0', 'hardware=1'), ('145 frames', '1 frames')]:
            with self.subTest(old=old):
                with self.assertRaises(ValueError):
                    self.validate(self.log.replace(old, new))
        with self.assertRaises(ValueError):
            self.validate(self.log+'A540 HARDWARE RENDER: FAIL late mismatch\n')

    def test_log_authenticity_is_required(self):
        with self.assertRaises(ValueError):
            check.validate(self.log.encode(), {'status': 'log_saved_fastboot_verified', 'sha256': 'wrong'}, {})


if __name__ == '__main__':
    unittest.main()
