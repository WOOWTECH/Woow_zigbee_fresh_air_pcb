/* CHIP 專案設定（sdkconfig.defaults 的 CONFIG_CHIP_PROJECT_CONFIG 指到這裡；整個 Matter 函式庫都會 include）。
 * 開發期沒有工廠分區時，Basic Information 的廠商／產品名稱用這裡的值（不設會是 TEST_VENDOR／TEST_PRODUCT，
 * HA、Apple、Google 都直接拿來當裝置名稱）。量產時由 esp-matter-mfg-tool 寫進 fctry 的值為準。
 * Matter 規定最多 32 bytes（UTF-8）。 */
#pragma once

#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME  "WOOWTECH"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "WO30109 新風控制器"
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "V3.4"
