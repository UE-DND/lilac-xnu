import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('boot_analysis', Path(__file__).resolve().parents[1]/'analyze-boot.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BootAnalysisTests(unittest.TestCase):
    def test_lifetime_clock_and_truncated_log(self):
        report = module.analyze('''FAST BOOT system-mounted uptime_ms=223
FAST BOOT system-mounted UFS read_ticks=1920000 tick_hz=19200000 errors=0
com.apple.xpc.launchd|2025-11-01 12:26:46.000000 <Notice>: hello
com.apple.xpc.launchd|2025-11-01 12:28:57.000000 (user/501/com.apple.migrationpluginwrapper [17]) <Notice>: Successfully spawned migrationpluginwrapper[17] because ipc (mach)
com.apple.xpc.launchd|2025-11-01 12:39:39.000000 (user/501/com.apple.migrationpluginwrapper [17]) <Notice>: exited due to exit(0)
com.apple.xpc.launchd|2025-11-01 12:39:40.000000 (user/501/com.apple.migrationpluginwrapper [20]) <Notice>: Successfully spawned migrationpluginwrapper[20] because ipc (mach)
[ 9999.000000]: MiniIOMobileFramebuffer: swap #1
''')
        self.assertEqual(report['stages'][0]['uptime_ms'], 223)
        self.assertEqual(report['storage'][0]['read_seconds'], .1)
        self.assertEqual(report['migration_wrappers'][0]['duration_seconds'], 642)
        self.assertIsNone(report['migration_wrappers'][1]['duration_seconds'])
        self.assertEqual(report['first_swap']['counter_seconds'], 9999)
        self.assertNotIn('uptime_ms', report['first_swap'])

    def test_collapsed_repeats_are_not_invented_lookup_counts(self):
        report = module.analyze('''com.apple.xpc.launchd|2025-11-01 12:29:02.000000 (user/501) <Warning>: failed lookup: name = com.apple.mobileactivationd, flags = 0x8, requestor = lsd[13], error = 3: No such process
com.apple.xpc.launchd|2025-11-01 12:30:58.000000 <Notice>: Last log repeated 63 times
''')
        self.assertEqual(report['failed_lookups'][0]['observed_lines'], 1)
        self.assertEqual(report['collapsed_repeats'][0]['count'], 63)

    def test_reused_pid_keeps_individual_migrations(self):
        report = module.analyze('\n'.join(
            f'com.apple.xpc.launchd|2025-11-01 12:00:{second:02d}.000000 (user/501/com.apple.migrationpluginwrapper [17]) <Notice>: {event}'
            for second, event in [(0, 'Successfully spawned migrationpluginwrapper[17]'),
                                  (2, 'exited due to exit(0)'),
                                  (5, 'Successfully spawned migrationpluginwrapper[17]'),
                                  (8, 'exited due to SIGKILL')]))
        self.assertEqual([span['duration_seconds'] for span in report['migration_wrappers']], [2, 3])


if __name__ == '__main__':
    unittest.main()
