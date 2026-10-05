#include "fa_coil.h"

uint8_t fa_coil_duty_pct(const fa_coil_cfg_t *c, bool on, uint32_t ms_since_on)
{
    if (!on) return 0;
    if (ms_since_on < c->pullin_ms) return 100;
    return c->hold_pct > 100 ? 100 : c->hold_pct;
}

uint32_t fa_coil_voltage_mv(const fa_coil_cfg_t *c, uint8_t duty_pct)
{
    if (duty_pct > 100) duty_pct = 100;
    int32_t v = ((int32_t)duty_pct * c->supply_mv - (int32_t)(100 - duty_pct) * c->diode_mv) / 100;
    return v > 0 ? (uint32_t)v : 0;
}

uint32_t fa_coil_supply_ua(const fa_coil_cfg_t *c, uint8_t duty_pct)
{
    if (duty_pct > 100) duty_pct = 100;
    uint32_t i_coil_ua = fa_coil_voltage_mv(c, duty_pct) * 1000u / c->coil_ohm;
    return i_coil_ua * duty_pct / 100u;
}

uint32_t fa_coil_budget_ua(const fa_coil_cfg_t *c, uint8_t held, uint8_t pulling,
                           uint32_t base_ua, uint32_t burst_ua)
{
    return base_ua + burst_ua + pulling * fa_coil_supply_ua(c, 100) + held * fa_coil_supply_ua(c, c->hold_pct);
}
