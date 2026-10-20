import pathlib
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
BAT=(ROOT/'src/battery_telemetry.cpp').read_text(encoding='utf-8-sig')
LIGHT=(ROOT/'src/zigbee_light.cpp').read_text(encoding='utf-8-sig')
CONFIG=(ROOT/'include/config.h').read_text(encoding='utf-8-sig')
BUILDER=(ROOT/'scripts/build_firmware.py').read_text(encoding='utf-8-sig')
INI=(ROOT/'platformio.ini').read_text(encoding='utf-8-sig')

class Zigbee3ConformanceTests(unittest.TestCase):
    def test_simple_sensor_declares_mandatory_server_clusters(self):
        self.assertIn('ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID',BAT)
        for cluster in ['ESP_ZB_ZCL_CLUSTER_ID_BASIC',
                        'esp_zb_binary_input_cluster_create',
                        'esp_zb_cluster_list_add_binary_input_cluster',
                        'esp_zb_cluster_list_add_identify_cluster']:
            self.assertIn(cluster,BAT)
        self.assertIn('ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE',BAT)
        self.assertIn('ESP_ZB_ZCL_CLUSTER_SERVER_ROLE',BAT)

    def test_binary_sensor_tracks_real_adc_validity(self):
        self.assertIn('input.present_value = percentageRaw != BATTERY_ZCL_UNKNOWN', BAT)
        self.assertIn('endpoint_.syncBatterySnapshot(',BAT)
        self.assertIn('ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID',BAT)
        self.assertIn('ESP_ZB_ZCL_ATTR_BINARY_INPUT_STATUS_FLAGS_ID',BAT)

    def test_device_versions_and_mandatory_dimmable_clusters(self):
        self.assertIn('endpoint_.setVersion(ZIGBEE_APP_VERSION)', BAT)
        self.assertIn('endpoint_.setVersion(ZIGBEE_APP_VERSION)', LIGHT)
        self.assertIn('ZIGBEE_APP_VERSION = 2',CONFIG)
        self.assertIn('ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID', LIGHT)
        for name in ('basic','identify','groups','scenes','on_off','level'):
            self.assertIn('esp_zb_cluster_list_add_'+name+'_cluster',LIGHT)

    def test_strict_default_and_no_cross_profile_security(self):
        self.assertIn('default_envs = esp32-h2-zigbee3-strict',INI)
        self.assertIn('ZIGBEE3_STRICT_SECURITY',INI)
        self.assertIn('SPRUT_TC_COMPAT_MODE',INI)
        self.assertIn('esp_zb_secur_link_key_exchange_required_set(true)',BUILDER)
        self.assertIn('esp_zb_secur_link_key_exchange_required_set(false)',BUILDER)
        self.assertIn('!defined(SPRUT_TC_COMPAT_MODE)',BUILDER)
        self.assertIn('!defined(ZIGBEE3_STRICT_SECURITY)',BUILDER)

    def test_local_zcl_level_outbound_range_and_inbound_zero_remembered(self):
        self.assertIn('level == 0 || level == 0xFF', LIGHT)
        self.assertIn('rawLevel == 0xFF ? level_ : rawLevel', LIGHT)
        self.assertIn('applyLightAttribute', LIGHT)
        self.assertIn('setInitialBatteryPowerSource()', LIGHT)

    def test_basic_swbuildid_distinguishes_profiles(self):
        self.assertIn('ZCL_SW_BUILD_ID[] = "0.9.0-a3-strict"',CONFIG)
        self.assertIn('ZCL_SW_BUILD_ID[] = "0.9.0-a3-sprut"',CONFIG)
        bid=(ROOT/'include/zigbee_build_id.h').read_text(encoding='utf-8-sig')
        self.assertIn('BeaconConfig::ZCL_SW_BUILD_ID',bid)

if __name__=='__main__':
    unittest.main()