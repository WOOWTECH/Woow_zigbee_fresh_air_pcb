/* DI／DO 模式（V3.2）：4 路 DI（J5 光耦輸入）與 433 遙控器 4 鍵同步，固定 1 對 1 控制 K1–K4。純 C，不碰硬體。
 *
 * DI 模式（每路）：
 *   全關     不控制繼電器（狀態照樣回報）
 *   按一次   接通的那一刻觸發一次（按住不重複，放開再按才算下一次）
 *   持續     接通期間作動，斷開就停止
 * DO 模式（每路）：
 *   自鎖     按一次：開↔關反轉；持續：接通＝開、斷開＝關
 *   點動     按一次：開 jog_ms 後自動關（期間再按重新計時）；持續：接通＝開、斷開＝關
 *   互鎖     設成互鎖的幾路自動成一組（取代指撥的風速模式）：開這一路會先關同組其他路（先斷後通）。
 *            按一次：沒開就開、已開就關；持續：接通＝開（同組其他關）、斷開＝關
 *
 * 遙控器第 k 鍵＝DI k 的分身：與接線 DI k 用同一組設定，狀態取「或」（任一接通就算接通）。
 * 遙控器沒有放開訊號：按住時每 40–64ms 重送一幀，超過 FA_IO_RF_RELEASE_MS 沒收到就當放開。
 * Zigbee／Matter／按鍵的指令直接操作繼電器（遵守互鎖與同時吸合上限），最後的指令為準。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fa_relays.h"

#define FA_IO_DEBOUNCE_SAMPLES 3      /* 接線 DI 每 10ms 取樣，連續 3 次相同才算（30ms） */
#define FA_IO_RF_RELEASE_MS    200

typedef enum { FA_DI_OFF = 0, FA_DI_PRESS = 1, FA_DI_HOLD = 2 } fa_di_mode_t;
typedef enum { FA_DO_LATCH = 0, FA_DO_JOG = 1, FA_DO_INTERLOCK = 2 } fa_do_mode_t;

typedef struct {
    uint8_t  di_mode[FA_CH];
    uint8_t  do_mode[FA_CH];
    uint32_t jog_ms[FA_CH];       /* 點動秒數，500ms–60 分鐘 */
} fa_io_cfg_t;

typedef struct {
    fa_io_cfg_t cfg;
    bool     wired[FA_CH];        /* 防彈跳後的接線 DI */
    uint8_t  deb_cnt[FA_CH];
    bool     deb_raw[FA_CH];
    bool     rf[FA_CH];           /* 遙控器按住中 */
    uint32_t rf_last[FA_CH];
    bool     active[FA_CH];       /* 接線 或 遙控器 */
    bool     jog_run[FA_CH];
    uint32_t jog_until[FA_CH];
} fa_io_t;

/* 出廠預設：preset 決定 DO 模式（三段風速＋1 路＝K1–K3 互鎖、K4 自鎖…），DI 全部「持續」，點動 1 秒 */
void    fa_io_default(fa_io_cfg_t *c, fa_mode_t preset);
bool    fa_io_cfg_valid(const fa_io_cfg_t *c);
uint8_t fa_io_interlock_mask(const fa_io_cfg_t *c);   /* 給 fa_relays_init_group */
void    fa_io_init(fa_io_t *io, const fa_io_cfg_t *c);

/* 每 10ms 餵一次接線 DI 原始電位（true＝接通）。回傳動作數，或 FA_ERR_LIMIT（同時吸合上限，狀態不變） */
int fa_io_wired(fa_io_t *io, fa_relays_t *r, uint8_t ch, bool closed, uint32_t now_ms, fa_action_t *out, int max_out);
/* 遙控器第 ch 鍵收到一幀 */
int fa_io_rf(fa_io_t *io, fa_relays_t *r, uint8_t ch, uint32_t now_ms, fa_action_t *out, int max_out);
/* 定期呼叫（≤50ms）：遙控器放開判定、點動到時 */
int fa_io_tick(fa_io_t *io, fa_relays_t *r, uint32_t now_ms, fa_action_t *out, int max_out);
