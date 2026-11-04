import copy
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lilaclib import hardware
from lilaclib.paths import HAKIOS


class HardwareTests(unittest.TestCase):
    def fixture(self):
        cells = lambda *values: struct.pack('>' + 'I' * len(values), *values)
        return {
            '': {'#address-cells': cells(2)},
            '/soc': {'#address-cells': cells(1), '#size-cells': cells(1), 'ranges': cells(0, 0, 0, 0xffffffff)},
            '/soc/i2c@c179000': {'compatible': b'qcom,i2c-msm-v2\0', 'reg': cells(0xc179000, 0x600),
                                'qcom,clk-freq-in': cells(19200000), 'pinctrl-0': cells(1, 2),
                                'clock-names': b'iface_clk\0core_clk\0', 'clocks': cells(3, 0x8caa5b4f, 3, 0xacae5604)},
            '/soc/qcom,gcc@100000': {'compatible': b'qcom,gcc-8998-v2\0', 'reg': cells(0x100000, 0xb0000),
                                    '#clock-cells': cells(1), 'phandle': cells(3)},
            '/soc/i2c@c179000/synaptics_clearpad@2c': {'reg': cells(0x2c), 'interrupts': cells(125, 2),
                                                     'compatible': b'synaptics,clearpad\0'},
            '/soc/qcom,kgsl-3d0@5000000': {'qcom,chipid': cells(0x05040001)},
            '/soc/qcom,gpucc@5065000': {'reg': cells(0x5065000, 0x9000)},
            '/soc/pinctrl@03400000': {'compatible': b'qcom,msm8998-pinctrl\0', 'reg': cells(0x3400000, 0xc00000),
                                     '#gpio-cells': cells(2), 'gpio-controller': b'', 'phandle': cells(4)},
            '/soc/qcom,mdss_dsi@0/qcom,mdss_dsi_ctrl0@c994000': {
                'qcom,platform-touch-reset-gpio': cells(4, 89, 0),
                'qcom,platform-touch-int-gpio': cells(4, 125, 0),
                'qcom,platform-touch-vddio-gpio': cells(4, 133, 0)},
            '/soc/pinctrl@03400000/msm_gpio_87': {'phandle': cells(1)},
            '/soc/pinctrl@03400000/msm_gpio_88': {'phandle': cells(2)},
            '/soc/pinctrl@03400000/msm_gpio_87/mux': {'pins': b'gpio87\0', 'function': b'blsp_i2c5\0'},
            '/soc/pinctrl@03400000/msm_gpio_88/mux': {'pins': b'gpio88\0', 'function': b'blsp_i2c5\0'},
        }

    def test_verified_resources(self):
        hardware.validate_resources(self.fixture())

    def test_each_missing_property_fails_closed(self):
        fixture = self.fixture()
        for node, properties in fixture.items():
            for key in properties:
                with self.subTest(node=node, key=key):
                    changed = copy.deepcopy(fixture)
                    del changed[node][key]
                    with self.assertRaises(ValueError):
                        hardware.validate_resources(changed)

    def test_each_changed_property_fails_closed(self):
        fixture = self.fixture()
        for node, properties in fixture.items():
            for key, value in properties.items():
                with self.subTest(node=node, key=key):
                    changed = copy.deepcopy(fixture)
                    changed[node][key] = bytes([value[0] ^ 1]) + value[1:] if value else b'changed'
                    with self.assertRaises(ValueError):
                        hardware.validate_resources(changed)

    def test_disabled_bus_is_rejected(self):
        nodes = self.fixture()
        nodes['/soc/i2c@c179000']['status'] = b'disabled\0'
        with self.assertRaises(ValueError):
            hardware.validate_resources(nodes)

    def test_disabled_gpio_is_rejected(self):
        nodes = self.fixture()
        nodes['/soc/pinctrl@03400000']['status'] = b'disabled\0'
        with self.assertRaises(ValueError):
            hardware.validate_resources(nodes)

    def test_phandle_renumbering(self):
        nodes = self.fixture()
        gpio = struct.pack('>I', 0x123)
        nodes['/soc/pinctrl@03400000']['phandle'] = gpio
        display = nodes['/soc/qcom,mdss_dsi@0/qcom,mdss_dsi_ctrl0@c994000']
        for key in display:
            display[key] = gpio + display[key][4:]
        gcc = struct.pack('>I', 0x321)
        nodes['/soc/qcom,gcc@100000']['phandle'] = gcc
        nodes['/soc/i2c@c179000']['clocks'] = gcc + struct.pack('>I', 0x8caa5b4f) + gcc + struct.pack('>I', 0xacae5604)
        hardware.validate_resources(nodes)

    def test_gpu_cx_supply_must_match(self):
        nodes = self.fixture()
        gpu = nodes['/soc/qcom,kgsl-3d0@5000000']
        gpu['reg'] = struct.pack('>II', 0x5000000, 0x40000)
        gpu['vddcx-supply'] = struct.pack('>I', 0x1234)
        nodes['/soc/qcom,gdsc@5066004'] = {
            'compatible': b'qcom,gdsc\0', 'reg': struct.pack('>IIII', 0x5066004, 4, 0x5066008, 4),
            'reg-names': b'base\0hw_ctrl_addr\0', 'phandle': struct.pack('>I', 0x1234)}
        nodes['/soc/arm,smmu-kgsl@5040000'] = {
            'compatible': b'qcom,smmu-v2\0', 'reg': struct.pack('>II', 0x5040000, 0x10000),
            'phandle': struct.pack('>I', 0x2345)}
        nodes['/soc/qcom,kgsl-iommu/gfx3d_user'] = {'iommus': struct.pack('>II', 0x2345, 0)}
        hardware.validate_gpu_cx(nodes)
        for key in ('compatible', 'reg', 'reg-names', 'phandle'):
            changed = copy.deepcopy(nodes)
            del changed['/soc/qcom,gdsc@5066004'][key]
            with self.assertRaises(ValueError):
                hardware.validate_gpu_cx(changed)
        gpu['vddcx-supply'] = struct.pack('>I', 0x9999)
        with self.assertRaises(ValueError):
            hardware.validate_gpu_cx(nodes)

    def test_gpu_deep_requires_verified_rpm_layout(self):
        node = '/soc/qcom,glink-smem-native-xprt-rpm@778000'
        nodes = {node: {'compatible': b'qcom,glink-rpm-native-xprt\0',
                        'reg': struct.pack('>IIII', 0x778000, 0x7000, 0x17911008, 4),
                        'reg-names': b'msgram\0irq-reg-base\0', 'qcom,irq-mask': struct.pack('>I', 1)}}
        hardware.validate_gpu_deep(nodes)
        for key in tuple(nodes[node]):
            bad = copy.deepcopy(nodes)
            del bad[node][key]
            with self.assertRaises(ValueError):
                hardware.validate_gpu_deep(bad)
        nodes[node]['status'] = b'disabled\0'
        with self.assertRaises(ValueError):
            hardware.validate_gpu_deep(nodes)

    def test_driver_namespaces_and_traversal(self):
        spec = importlib.util.spec_from_file_location('test_hakios_config', HAKIOS / 'tools/hakios-config.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'drivers/input/conf'
            config.parent.mkdir(parents=True)
            for filename in ('lilac.touch.c', 'hakios.input.c'):
                config.write_text('standard bsd/dev/' + filename + '\n')
                self.assertEqual(module.load_drivers(directory)[1][0][0], 'bsd/dev/' + filename)
            for filename in ('lilac../outside.c', 'lilac.c', 'other.touch.c', 'lilac.touch.h'):
                config.write_text('standard bsd/dev/' + filename + '\n')
                with self.assertRaises(module.ConfigError):
                    module.load_drivers(directory)

    def test_zap_requires_correct_firmware_and_memory_ownership(self):
        path = '/soc/qcom,kgsl-hyp'
        nodes = {path: {'compatible': b'qcom,pil-tz-generic\0',
                        'qcom,pas-id': struct.pack('>I', 13),
                        'qcom,firmware-name': b'a540_zap\0'},
                 '/soc/qcom,kgsl-3d0@5000000': {'qcom,zap-shader': b'a540_zap\0'}}
        hardware.validate_gpu_zap(nodes)
        for name in nodes[path]:
            bad = copy.deepcopy(nodes)
            del bad[path][name]
            with self.assertRaises(ValueError):
                hardware.validate_gpu_zap(bad)
        for key, value in (('status', b'disabled\0'), ('memory-region', struct.pack('>I', 1)),
                           ('qcom,pas-id', struct.pack('>I', 12))):
            bad = copy.deepcopy(nodes)
            bad[path][key] = value
            with self.assertRaises(ValueError):
                hardware.validate_gpu_zap(bad)


if __name__ == '__main__':
    unittest.main()
