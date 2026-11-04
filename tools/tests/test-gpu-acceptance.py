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

    def validate(self, text, extended=False):
        data = text.encode()
        receipt = {'status': 'log_saved_fastboot_verified', 'bytes': len(data),
                   'sha256': hashlib.sha256(data).hexdigest()}
        return check.validate(data, receipt, {'partition_writes': False, 'firmware_updates': False}, extended)

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

    def test_extended_evidence_and_late_failures(self):
        extra = 'SMP WORK: PASS active=8 executed_mask=ff permission_changes=512 data_errors=0\n'
        extra += ''.join(f'SMP IRQ: cpu={i} IPI=10 timer=100\n' for i in range(8))
        extra += ''.join(f'psci: cpu {i} firmware confirmed OFF\n' for i in range(1,8))
        extra += 'A540 PROCESS ISOLATION: PASS same VA=4000000 peer=4000000\n'
        for index,address in enumerate(('2018040','2000000','4010000','831')):
            extra += f'A540 PROTECTION: PASS target={address} rejected and next client completed\n'
            extra += f'submissions disabled: context hardware fault\nquarantined context={index+1} canceled_through={index+1} reset confirmed\n'
            extra += f'final render state FSR=408 FAR={address} RBBM=1\n'
        extra += 'clock: idle=180000000 Hz\nclock: active=257000000 Hz\n'
        extra += 'render faults: CP interrupt=0 hardware=0 protection=50020c4\n'
        extra += 'PM: suspended sequence=1\nPM: resumed sequence=1\n' * 3
        extra += 'authenticated image 13, dedicated memory=a6000000\n' * 4
        text=extra+self.log
        self.assertEqual(self.validate(text,True)['status'],'passed')
        for old,new in [('cpu=7 IPI=10','cpu=7 IPI=0'),('executed_mask=ff','executed_mask=7f'),
                        ('target=831 rejected','target=832 rejected'),('FAR=2018040','FAR=0'),
                        ('cpu 7 firmware confirmed OFF','cpu 7 unknown'),('permission_changes=512','permission_changes=16'),
                        ('protection=50020c4','protection=0')]:
            with self.subTest(old=old), self.assertRaises(ValueError): self.validate(text.replace(old,new),True)
        with self.assertRaises(ValueError): self.validate(text+'submissions disabled: unexplained late fault\n',True)
        with self.assertRaises(ValueError): self.validate(self.log+'render faults: CP interrupt=0 hardware=1 protection=0\n')
        # Runtime PM adds earlier stops; only the final one proves shutdown.
        self.assertEqual(self.validate('A540 stop: sequence=0 completed=0 MMU=1 memory=1 PM8005=1 MX=1 CX=1\n'+self.log)['submissions_completed'],100)

    def test_log_authenticity_is_required(self):
        with self.assertRaises(ValueError):
            check.validate(self.log.encode(), {'status': 'log_saved_fastboot_verified', 'sha256': 'wrong'}, {})


if __name__ == '__main__':
    unittest.main()
