import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lilaclib.ramboot import validate_usb_debug_nodes


def fixture():
    def cells(*values):
        return struct.pack('>' + 'I' * len(values), *values)
    return {
        '': {'#address-cells': cells(2)},
        '/soc': {'#address-cells': cells(1), '#size-cells': cells(1), 'ranges': cells(0, 0, 0, 0xffffffff)},
        '/psci': {'method': b'smc\0'},
        '/soc/ssusb@a800000': {'reg': cells(0xa800000, 0xf8c00, 0xc016000, 0x400), 'ranges': b'',
                             '#address-cells': cells(1), '#size-cells': cells(1)},
        '/soc/ssusb@a800000/dwc3@a800000': {'compatible': b'snps,dwc3\0', 'reg': cells(0xa800000, 0xcd00),
                                         'maximum-speed': b'high-speed\0'},
        '/soc/qusb@c012000': {'reg': cells(0xc012000, 0x2a8, 0x1fcb24c, 4, 0x784238, 4)},
        '/soc/qcom,msm-imem@146bf000': {'#address-cells': cells(1), '#size-cells': cells(1),
                                     'ranges': cells(0, 0x146bf000, 0x1000)},
        '/soc/qcom,msm-imem@146bf000/restart_reason@65c': {'reg': cells(0x65c, 4)},
        '/soc/restart@10ac000': {'compatible': b'qcom,pshold\0', 'reg': cells(0x10ac000, 4, 0x1fd3000, 4)},
        '/soc/qcom,spmi@800f000': {'compatible': b'qcom,spmi-pmic-arb\0', 'qcom,ee': cells(0),
                                 'qcom,reserved-chan': cells(511), 'reg-names': b'core\0chnls\0obsrvr\0intr\0cnfg\0',
                                 'reg': cells(0x800f000, 0x1000, 0x8400000, 0x1000000, 0x9400000, 0x1000000,
                                              0xa400000, 0x220000, 0x800a000, 0x3000)},
        '/soc/qcom,spmi@800f000/qcom,pm8998@0': {'reg': cells(0, 0)},
        '/soc/qcom,spmi@800f000/qcom,pm8998@0/qcom,power-on@800': {
            'compatible': b'qcom,qpnp-power-on\0', 'reg': cells(0x800, 0x100),
            'qcom,system-reset': b'', 'qcom,store-hard-reset-reason': b''},
    }


class DebugResourcesTests(unittest.TestCase):
    def test_verified_resources(self):
        validate_usb_debug_nodes(fixture())

    def test_missing_changed_or_disabled_resources_fail(self):
        nodes = fixture()
        for node, properties in nodes.items():
            for key in properties:
                for missing in (True, False):
                    with self.subTest(node=node, key=key, missing=missing):
                        changed = copy.deepcopy(nodes)
                        if missing:
                            del changed[node][key]
                        else:
                            changed[node][key] += b'\0'
                        with self.assertRaises(ValueError):
                            validate_usb_debug_nodes(changed)
            changed = copy.deepcopy(nodes)
            changed[node]['status'] = b'disabled\0'
            with self.assertRaises(ValueError):
                validate_usb_debug_nodes(changed)

    def test_unsupported_pmic_reset_topology(self):
        for key in ('qcom,secondary-pon-reset', 'qcom,warm-reset-poweroff-type', 'qcom,system-reset'):
            nodes = fixture()
            nodes['/soc/other-pon'] = {'compatible': b'qcom,qpnp-power-on\0', key: b''}
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_usb_debug_nodes(nodes)


if __name__ == '__main__':
    unittest.main()
