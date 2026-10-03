/* Zigbee 3.0 路由器：4 個 Mains Power Outlet 端點（EP1–4 = 繼電器 1–4）。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FA_ZB_EP_FIRST 1

/* 協調器要求開/關第 ch 路；回傳 false = 拒絕（例如超過同時吸合上限），屬性會被打回原值 */
typedef bool (*fa_zb_set_cb_t)(uint8_t ch, bool on);

void fa_zigbee_start(fa_zb_set_cb_t on_set);
bool fa_zigbee_joined(void);
/* 本地（按鍵／遙控器／互鎖）改變狀態後，把 4 個端點的 OnOff 屬性同步上去（會觸發回報） */
void fa_zigbee_sync(const bool on[4]);
/* 離開網路並清除 Zigbee 資料，完成後重開機重新配對 */
void fa_zigbee_factory_reset(void);
