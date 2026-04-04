/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
    SELECTOR_OFF = 0,
    SELECTOR_P1,
    SELECTOR_P2,
    SELECTOR_INVALID
} SelectorState_t;

typedef enum
{
    PUMP_STATE_OFF = 0,
    PUMP_STATE_START_WAIT_AC,
    PUMP_STATE_START_WAIT_RPM,
    PUMP_STATE_RUN_WAIT_PRESSURE_RECOVERY,
    PUMP_STATE_RUNNING,
    PUMP_STATE_FAULT
} PumpState_t;

typedef struct
{
    uint8_t pressure_p1_raw;
    uint8_t rpm_p1_raw;
    uint8_t pressure_p2_raw;
    uint8_t rpm_p2_raw;

    uint8_t ac_p1_raw;
    uint8_t ac_p2_raw;

    uint8_t ack_lt_raw;
    uint8_t sel_p1_raw;
    uint8_t sel_p2_raw;
} RawInputs_t;

typedef struct
{
    uint8_t pressure_p1;
    uint8_t rpm_p1;
    uint8_t pressure_p2;
    uint8_t rpm_p2;

    uint8_t ac_p1;
    uint8_t ac_p2;

    uint8_t ack_short;
    uint8_t lamp_test;
    SelectorState_t selector;
} Inputs_t;

typedef struct
{
    uint8_t failure_ams;
    uint8_t pump1_cmd;
    uint8_t pump2_cmd;

    uint8_t ind1_system_ready;
    uint8_t ind2_p1_ready;
    uint8_t ind3_p1_on;
    uint8_t ind4_p1_standby;
    uint8_t ind5_p2_ready;
    uint8_t ind6_p2_on;
    uint8_t ind7_p2_standby;
    uint8_t ind8_pressure_low;
    uint8_t ind9_standby_alarm;
} Outputs_t;

typedef struct
{
    PumpState_t state;
    uint32_t state_tick;
} PumpChannel_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define INPUT_ACTIVE_STATE GPIO_PIN_SET
#define LED_ON_STATE GPIO_PIN_SET
#define LED_OFF_STATE ((LED_ON_STATE == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define PIN_IS_ACTIVE(port, pin) (HAL_GPIO_ReadPin((port), (pin)) == (INPUT_ACTIVE_STATE))
#define MAYBE_UNUSED __attribute__((unused))

#define SR_LATCH_GPIO SR_LATCH_GPIO_Port
#define SR_LATCH_PIN SR_LATCH_Pin
#define SR_OE_GPIO SR_OE_GPIO_Port
#define SR_OE_PIN SR_OE_Pin

#define K_ACTIVE_LOW 0

/* ---------------- Configuration ---------------- */
#define APP_MODE_SINGLE_PUMP_CFG 0U
#define APP_MODE_DUAL_PUMP_CFG 1U
#define APP_MODE APP_MODE_DUAL_PUMP_CFG

/* Choose one firmware behavior:
   CONTROL_MODE_SELECTOR_DUAL_CFG = original selector-driven controller
   CONTROL_MODE_BYPASS_DUAL_CFG   = temporary bypass, pump 1 and pump 2 run independently
   CONTROL_MODE_OUTPUT_TEST_CFG   = relay and indicator IO test firmware
*/
#define CONTROL_MODE_SELECTOR_DUAL_CFG 0U
#define CONTROL_MODE_BYPASS_DUAL_CFG 1U
#define CONTROL_MODE_OUTPUT_TEST_CFG 2U

/* Change only this line to select the firmware behavior. */
#define CONTROL_MODE CONTROL_MODE_SELECTOR_DUAL_CFG

/* Change this to adjust the output-test repeat time. */
#define OUTPUT_TEST_REPEAT_MS 200U

/* Input polarities: set 1 if active high, 0 if active low */
#define PRESSURE_ACTIVE_LEVEL 0U
#define RPM_ACTIVE_LEVEL 0U
#define AC_ACTIVE_LEVEL 0U
#define SELECTOR_ACTIVE_LEVEL 1U
#define ACK_LT_ACTIVE_LEVEL 0U

/* Timing */
#define LOOP_DELAY_MS 20U
#define T_DEBOUNCE_MS 200U
#define T_ACK_DEBOUNCE_MS 50U
#define T_ACK_LONGPRESS_MS 1500U
#define T_BLINK_MS 500U
#define T_PRESSURE_TIMEOUT_MS 10000U
#define T_OUTPUT_TEST_STEP_MS OUTPUT_TEST_REPEAT_MS

#define FAULT_SEL_INVALID_MASK (1UL << 0)
#define FAULT_P1_NOT_READY_MASK (1UL << 1)
#define FAULT_P2_NOT_READY_MASK (1UL << 2)
#define FAULT_P1_PRESSURE_TIMEOUT_MASK (1UL << 3)
#define FAULT_P2_PRESSURE_TIMEOUT_MASK (1UL << 4)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */

static RawInputs_t g_raw;
static Inputs_t g_in;
static Outputs_t g_out;

static PumpState_t g_state = PUMP_STATE_OFF;
static uint8_t g_alarm_latched = 0;
static uint8_t g_alarm_blink = 0;
static uint32_t g_state_tick = 0;
static SelectorState_t g_active_pump = SELECTOR_OFF;

static uint32_t g_fault_active_mask = 0U;
static uint32_t g_fault_new_mask = 0U;
static uint32_t g_fault_prev_active_mask = 0U;
static uint32_t g_fault_latched_mask = 0U;

static PumpChannel_t g_p1_channel = {PUMP_STATE_OFF, 0U};
static PumpChannel_t g_p2_channel = {PUMP_STATE_OFF, 0U};

static uint8_t g_last_ack_raw = 0;
static uint32_t g_ack_press_tick = 0;
static uint8_t g_lamp_test_active = 0;
static MAYBE_UNUSED uint8_t g_test_relay_step = 0U;
static MAYBE_UNUSED uint8_t g_test_led_step = 0U;
static MAYBE_UNUSED uint32_t g_test_relay_tick = 0U;
static MAYBE_UNUSED uint32_t g_test_led_tick = 0U;
static uint8_t g_test_sys_led1 = 0U;
static uint8_t g_test_sys_led2 = 0U;

/* Debounced values */
static uint8_t db_pressure_p1 = 0;
static uint8_t db_rpm_p1 = 0;
static uint8_t db_pressure_p2 = 0;
static uint8_t db_rpm_p2 = 0;
static uint8_t db_ac_p1 = 0;
static uint8_t db_ac_p2 = 0;
static uint8_t db_sel_p1 = 0;
static uint8_t db_sel_p2 = 0;
static uint8_t db_ack_lt = 0;

static uint32_t db_tick_pressure_p1 = 0;
static uint32_t db_tick_rpm_p1 = 0;
static uint32_t db_tick_pressure_p2 = 0;
static uint32_t db_tick_rpm_p2 = 0;
static uint32_t db_tick_ac_p1 = 0;
static uint32_t db_tick_ac_p2 = 0;
static uint32_t db_tick_sel_p1 = 0;
static uint32_t db_tick_sel_p2 = 0;
static uint32_t db_tick_ack_lt = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

static void ReadRawInputs(void);
static void ProcessInputs(void);
static void UpdateSysLeds(void);

static void SR_LatchPulse(void);
static void SR_Write24(uint8_t u1, uint8_t u2, uint8_t u3);
static void UpdateOutputs(void);

static uint8_t NormalizeLevel(uint8_t raw_active, uint8_t active_level);
static void DebounceBit(uint8_t raw, uint8_t *db, uint32_t *tick, uint32_t debounce_ms);

static void ClearOutputs(void);
static void RunControlLogic(void);
static MAYBE_UNUSED void RunSinglePumpLogic(void);
static MAYBE_UNUSED void RunDualPumpLogic(void);
static MAYBE_UNUSED void RunBypassDualLogic(void);
static MAYBE_UNUSED void RunOutputTestProgram(void);
static void RunPumpChannel(PumpChannel_t *channel,
                           uint8_t pressure_low,
                           uint8_t not_ready_fault_active,
                           uint8_t not_ready_fault_new,
                           uint8_t rpm_active,
                           uint8_t *pump_cmd);

static void StopAllPumps(void);
static void EnterState(PumpState_t new_state);
static void EnterPumpChannelState(PumpChannel_t *channel, PumpState_t new_state);
static void PrimeDebouncedInputs(void);

static uint8_t PressureDemandActive(void);
static uint8_t PressureDemandForPump(SelectorState_t pump);
static uint8_t PumpRunRequest(uint8_t pressure_low, uint8_t rpm_active);
static uint8_t PumpStateIsRunning(PumpState_t state);
static MAYBE_UNUSED uint32_t ComputeSinglePumpFaultMask(void);
static uint32_t ComputeDualPumpFaultMask(void);
static MAYBE_UNUSED uint32_t ComputeBypassFaultMask(void);
static void UpdateAlarmLatch(uint32_t active_mask);
static uint8_t SelectedPumpIsP1(void);
static uint8_t SelectedPumpIsP2(void);

static MAYBE_UNUSED uint8_t P1Ready(void);
static MAYBE_UNUSED uint8_t P2Ready(void);

static void ApplyLampTestIfNeeded(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t NormalizeLevel(uint8_t raw_active, uint8_t active_level)
{
    return active_level ? raw_active : (uint8_t)!raw_active;
}

static void DebounceBit(uint8_t raw, uint8_t *db, uint32_t *tick, uint32_t debounce_ms)
{
    uint32_t now = HAL_GetTick();

    if (raw != *db)
    {
        if ((now - *tick) >= debounce_ms)
        {
            *db = raw;
            *tick = now;
        }
    }
    else
    {
        *tick = now;
    }
}

static void ReadRawInputs(void)
{
    /* New DC order:
       I1 = unused / reserved (not read)
       I2 = Output Pump 1     (not read)
       I3 = Output Pump 2     (not read)
       I4 = Pressure switch Pump 1
       I5 = RPM switch Pump 1
       I6 = Pressure switch Pump 2
       I7 = RPM switch Pump 2
       I8 = ACK / Lamp Test
    */

    g_raw.pressure_p1_raw = PIN_IS_ACTIVE(I4_GPIO_Port, I4_Pin);
    g_raw.rpm_p1_raw = PIN_IS_ACTIVE(I5_GPIO_Port, I5_Pin);

    g_raw.pressure_p2_raw = PIN_IS_ACTIVE(I6_GPIO_Port, I6_Pin);
    g_raw.rpm_p2_raw = PIN_IS_ACTIVE(I7_GPIO_Port, I7_Pin);

    g_raw.ac_p1_raw = PIN_IS_ACTIVE(AC1_IN_GPIO_Port, AC1_IN_Pin);
    g_raw.ac_p2_raw = PIN_IS_ACTIVE(AC2_IN_GPIO_Port, AC2_IN_Pin);

    g_raw.ack_lt_raw = PIN_IS_ACTIVE(ACK_LT_GPIO_Port, ACK_LT_Pin);
    g_raw.sel_p1_raw = PIN_IS_ACTIVE(SEL_P1_GPIO_Port, SEL_P1_Pin);
    g_raw.sel_p2_raw = PIN_IS_ACTIVE(SEL_P2_GPIO_Port, SEL_P2_Pin);
}

static void ProcessInputs(void)
{
    uint32_t now = HAL_GetTick();

    DebounceBit(g_raw.pressure_p1_raw, &db_pressure_p1, &db_tick_pressure_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.rpm_p1_raw, &db_rpm_p1, &db_tick_rpm_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.pressure_p2_raw, &db_pressure_p2, &db_tick_pressure_p2, T_DEBOUNCE_MS);
    DebounceBit(g_raw.rpm_p2_raw, &db_rpm_p2, &db_tick_rpm_p2, T_DEBOUNCE_MS);
    DebounceBit(g_raw.ac_p1_raw, &db_ac_p1, &db_tick_ac_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.ac_p2_raw, &db_ac_p2, &db_tick_ac_p2, T_DEBOUNCE_MS);
    DebounceBit(g_raw.sel_p1_raw, &db_sel_p1, &db_tick_sel_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.sel_p2_raw, &db_sel_p2, &db_tick_sel_p2, T_DEBOUNCE_MS);
    DebounceBit(g_raw.ack_lt_raw, &db_ack_lt, &db_tick_ack_lt, T_ACK_DEBOUNCE_MS);

    g_in.pressure_p1 = NormalizeLevel(db_pressure_p1, PRESSURE_ACTIVE_LEVEL);
    g_in.rpm_p1 = NormalizeLevel(db_rpm_p1, RPM_ACTIVE_LEVEL);
    g_in.pressure_p2 = NormalizeLevel(db_pressure_p2, PRESSURE_ACTIVE_LEVEL);
    g_in.rpm_p2 = NormalizeLevel(db_rpm_p2, RPM_ACTIVE_LEVEL);

    g_in.ac_p1 = NormalizeLevel(db_ac_p1, AC_ACTIVE_LEVEL);
    g_in.ac_p2 = NormalizeLevel(db_ac_p2, AC_ACTIVE_LEVEL);

    {
        uint8_t sel1 = NormalizeLevel(db_sel_p1, SELECTOR_ACTIVE_LEVEL);
        uint8_t sel2 = NormalizeLevel(db_sel_p2, SELECTOR_ACTIVE_LEVEL);

        if ((sel1 == 0U) && (sel2 == 0U))
            g_in.selector = SELECTOR_OFF;
        else if ((sel1 == 1U) && (sel2 == 0U))
            g_in.selector = SELECTOR_P1;
        else if ((sel1 == 0U) && (sel2 == 1U))
            g_in.selector = SELECTOR_P2;
        else
            g_in.selector = SELECTOR_INVALID;
    }

    g_in.ack_short = 0U;
    g_in.lamp_test = 0U;

    {
        uint8_t ack_now = NormalizeLevel(db_ack_lt, ACK_LT_ACTIVE_LEVEL);

        if ((ack_now == 1U) && (g_last_ack_raw == 0U))
        {
            g_ack_press_tick = now;
            /* ACK happens immediately on the press edge.
               A long hold still becomes lamp test, so long press = ACK + lamp test. */
            g_in.ack_short = 1U;
            g_lamp_test_active = 0U;
        }

        if (ack_now == 1U)
        {
            if ((now - g_ack_press_tick) >= T_ACK_LONGPRESS_MS)
            {
                g_lamp_test_active = 1U;
            }
        }
        else
        {
            g_lamp_test_active = 0U;
        }

        g_in.lamp_test = g_lamp_test_active;
        g_last_ack_raw = ack_now;
    }

    g_alarm_blink = (((now / T_BLINK_MS) & 0x01U) != 0U) ? 1U : 0U;
}

static void UpdateSysLeds(void)
{
#if (CONTROL_MODE == CONTROL_MODE_OUTPUT_TEST_CFG)
    HAL_GPIO_WritePin(SYS_LED1_GPIO_Port, SYS_LED1_Pin,
                      g_test_sys_led1 ? LED_ON_STATE : LED_OFF_STATE);
    HAL_GPIO_WritePin(SYS_LED2_GPIO_Port, SYS_LED2_Pin,
                      g_test_sys_led2 ? LED_ON_STATE : LED_OFF_STATE);
#else
    HAL_GPIO_WritePin(SYS_LED1_GPIO_Port, SYS_LED1_Pin, LED_OFF_STATE);
    HAL_GPIO_WritePin(SYS_LED2_GPIO_Port, SYS_LED2_Pin,
                      g_alarm_blink ? LED_ON_STATE : LED_OFF_STATE);
#endif
}

static void SR_LatchPulse(void)
{
    HAL_GPIO_WritePin(SR_LATCH_GPIO, SR_LATCH_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SR_LATCH_GPIO, SR_LATCH_PIN, GPIO_PIN_RESET);
}

static void SR_Write24(uint8_t u1, uint8_t u2, uint8_t u3)
{
    uint8_t tx[3];

    if (K_ACTIVE_LOW)
    {
        u1 = (uint8_t)~u1;
        u2 = (uint8_t)~u2;
        u3 = (uint8_t)~u3;
    }

    /* Physical chain: MCU -> U1 -> U3 -> U6 */
    tx[0] = u3; // U6 = LED2 (farthest)
    tx[1] = u2; // U3 = LED1
    tx[2] = u1; // U1 = relay (nearest)

    /* Disable outputs (OE HIGH) */
    HAL_GPIO_WritePin(SR_OE_GPIO, SR_OE_PIN, GPIO_PIN_SET);

    /* Shift data */
    if (HAL_SPI_Transmit(&hspi1, tx, 3, 10U) != HAL_OK)
    {
        return;
    }

    /* Latch */
    SR_LatchPulse();

    /* Enable outputs (OE LOW) */
    HAL_GPIO_WritePin(SR_OE_GPIO, SR_OE_PIN, GPIO_PIN_RESET);
}

static void ClearOutputs(void)
{
    memset(&g_out, 0, sizeof(g_out));
}

static void StopAllPumps(void)
{
    g_out.pump1_cmd = 0U;
    g_out.pump2_cmd = 0U;
}

static void EnterState(PumpState_t new_state)
{
    if (g_state != new_state)
    {
        g_state = new_state;
        g_state_tick = HAL_GetTick();
        if ((new_state == PUMP_STATE_OFF) || (new_state == PUMP_STATE_FAULT))
        {
            g_active_pump = SELECTOR_OFF;
        }
    }
}

static void EnterPumpChannelState(PumpChannel_t *channel, PumpState_t new_state)
{
    if (channel->state != new_state)
    {
        channel->state = new_state;
        channel->state_tick = HAL_GetTick();
    }
}

static void PrimeDebouncedInputs(void)
{
    uint32_t now = HAL_GetTick();

    db_pressure_p1 = g_raw.pressure_p1_raw;
    db_rpm_p1 = g_raw.rpm_p1_raw;
    db_pressure_p2 = g_raw.pressure_p2_raw;
    db_rpm_p2 = g_raw.rpm_p2_raw;
    db_ac_p1 = g_raw.ac_p1_raw;
    db_ac_p2 = g_raw.ac_p2_raw;
    db_sel_p1 = g_raw.sel_p1_raw;
    db_sel_p2 = g_raw.sel_p2_raw;
    db_ack_lt = g_raw.ack_lt_raw;

    db_tick_pressure_p1 = now;
    db_tick_rpm_p1 = now;
    db_tick_pressure_p2 = now;
    db_tick_rpm_p2 = now;
    db_tick_ac_p1 = now;
    db_tick_ac_p2 = now;
    db_tick_sel_p1 = now;
    db_tick_sel_p2 = now;
    db_tick_ack_lt = now;
}

static uint8_t PressureDemandForPump(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
        return g_in.pressure_p1;
    if (pump == SELECTOR_P2)
        return g_in.pressure_p2;
    return 0U;
}

static uint8_t PumpRunRequest(uint8_t pressure_low, uint8_t rpm_active)
{
    return (uint8_t)(pressure_low || !rpm_active);
}

static uint8_t PumpStateIsRunning(PumpState_t state)
{
    switch (state)
    {
    case PUMP_STATE_START_WAIT_AC:
    case PUMP_STATE_START_WAIT_RPM:
    case PUMP_STATE_RUN_WAIT_PRESSURE_RECOVERY:
    case PUMP_STATE_RUNNING:
        return 1U;
    case PUMP_STATE_OFF:
    case PUMP_STATE_FAULT:
    default:
        return 0U;
    }
}

static MAYBE_UNUSED uint32_t ComputeSinglePumpFaultMask(void)
{
    uint32_t fault_mask = 0U;

    if (PumpRunRequest(g_in.pressure_p1, g_in.rpm_p1) && (g_in.ac_p1 == 0U))
    {
        fault_mask |= FAULT_P1_NOT_READY_MASK;
    }

    if (PumpStateIsRunning(g_state) &&
        g_in.pressure_p1 &&
        ((HAL_GetTick() - g_state_tick) >= T_PRESSURE_TIMEOUT_MS))
    {
        fault_mask |= FAULT_P1_PRESSURE_TIMEOUT_MASK;
    }

    return fault_mask;
}

static uint32_t ComputeDualPumpFaultMask(void)
{
    uint32_t fault_mask = 0U;

    if (g_in.selector == SELECTOR_INVALID)
    {
        fault_mask |= FAULT_SEL_INVALID_MASK;
    }
    else if ((g_in.selector == SELECTOR_P1) &&
             PumpRunRequest(g_in.pressure_p1, g_in.rpm_p1) &&
             (g_in.ac_p1 == 0U))
    {
        fault_mask |= FAULT_P1_NOT_READY_MASK;
    }
    else if ((g_in.selector == SELECTOR_P2) &&
             PumpRunRequest(g_in.pressure_p2, g_in.rpm_p2) &&
             (g_in.ac_p2 == 0U))
    {
        fault_mask |= FAULT_P2_NOT_READY_MASK;
    }

    if (SelectedPumpIsP1() &&
        PumpStateIsRunning(g_state) &&
        g_in.pressure_p1 &&
        ((HAL_GetTick() - g_state_tick) >= T_PRESSURE_TIMEOUT_MS))
    {
        fault_mask |= FAULT_P1_PRESSURE_TIMEOUT_MASK;
    }

    if (SelectedPumpIsP2() &&
        PumpStateIsRunning(g_state) &&
        g_in.pressure_p2 &&
        ((HAL_GetTick() - g_state_tick) >= T_PRESSURE_TIMEOUT_MS))
    {
        fault_mask |= FAULT_P2_PRESSURE_TIMEOUT_MASK;
    }

    return fault_mask;
}

static MAYBE_UNUSED uint32_t ComputeBypassFaultMask(void)
{
    uint32_t fault_mask = 0U;

    if (PumpRunRequest(g_in.pressure_p1, g_in.rpm_p1) && (g_in.ac_p1 == 0U))
    {
        fault_mask |= FAULT_P1_NOT_READY_MASK;
    }

    if (PumpRunRequest(g_in.pressure_p2, g_in.rpm_p2) && (g_in.ac_p2 == 0U))
    {
        fault_mask |= FAULT_P2_NOT_READY_MASK;
    }

    if (PumpStateIsRunning(g_p1_channel.state) &&
        g_in.pressure_p1 &&
        ((HAL_GetTick() - g_p1_channel.state_tick) >= T_PRESSURE_TIMEOUT_MS))
    {
        fault_mask |= FAULT_P1_PRESSURE_TIMEOUT_MASK;
    }

    if (PumpStateIsRunning(g_p2_channel.state) &&
        g_in.pressure_p2 &&
        ((HAL_GetTick() - g_p2_channel.state_tick) >= T_PRESSURE_TIMEOUT_MS))
    {
        fault_mask |= FAULT_P2_PRESSURE_TIMEOUT_MASK;
    }

    return fault_mask;
}

static void UpdateAlarmLatch(uint32_t active_mask)
{
    g_fault_new_mask = active_mask & ~g_fault_prev_active_mask;

    if (g_in.ack_short)
    {
        g_fault_latched_mask = 0U;
    }

    g_fault_active_mask = active_mask;
    g_fault_latched_mask |= g_fault_new_mask;
    g_fault_prev_active_mask = active_mask;
    g_alarm_latched = (g_fault_latched_mask != 0U) ? 1U : 0U;
}

static uint8_t PressureDemandActive(void)
{
#if (APP_MODE == APP_MODE_SINGLE_PUMP_CFG)
    return g_in.pressure_p1;
#else
    if (g_active_pump != SELECTOR_OFF)
    {
        return PressureDemandForPump(g_active_pump);
    }
    return PressureDemandForPump(g_in.selector);
#endif
}

static uint8_t SelectedPumpIsP1(void)
{
#if (APP_MODE == APP_MODE_SINGLE_PUMP_CFG)
    return 1U;
#else
    return (g_active_pump == SELECTOR_P1);
#endif
}

static uint8_t SelectedPumpIsP2(void)
{
#if (APP_MODE == APP_MODE_SINGLE_PUMP_CFG)
    return 0U;
#else
    return (g_active_pump == SELECTOR_P2);
#endif
}

static uint8_t P1Ready(void)
{
    return g_in.ac_p1;
}

static uint8_t P2Ready(void)
{
#if (APP_MODE == APP_MODE_SINGLE_PUMP_CFG)
    return 0U;
#else
    return g_in.ac_p2;
#endif
}

static void RunPumpChannel(PumpChannel_t *channel,
                           uint8_t pressure_low,
                           uint8_t channel_fault_active,
                           uint8_t channel_fault_new,
                           uint8_t rpm_active,
                           uint8_t *pump_cmd)
{
    uint8_t run_request = PumpRunRequest(pressure_low, rpm_active);

    if (g_in.ack_short)
    {
        if (channel->state == PUMP_STATE_FAULT)
        {
            EnterPumpChannelState(channel, PUMP_STATE_OFF);
        }
    }

    switch (channel->state)
    {
    case PUMP_STATE_OFF:
        *pump_cmd = 0U;
        if (run_request)
        {
            if (channel_fault_active == 0U)
            {
                *pump_cmd = 1U;
                EnterPumpChannelState(channel, PUMP_STATE_RUNNING);
            }
            else if (channel_fault_new != 0U)
            {
                EnterPumpChannelState(channel, PUMP_STATE_FAULT);
            }
        }
        break;

    case PUMP_STATE_START_WAIT_AC:
    case PUMP_STATE_START_WAIT_RPM:
    case PUMP_STATE_RUN_WAIT_PRESSURE_RECOVERY:
    case PUMP_STATE_RUNNING:
        if (run_request)
        {
            if (channel_fault_active == 0U)
            {
                *pump_cmd = 1U;
                EnterPumpChannelState(channel, PUMP_STATE_RUNNING);
            }
            else
            {
                *pump_cmd = 0U;
                if (channel_fault_new != 0U)
                {
                    EnterPumpChannelState(channel, PUMP_STATE_FAULT);
                }
                else
                {
                    EnterPumpChannelState(channel, PUMP_STATE_OFF);
                }
            }
        }
        else
        {
            *pump_cmd = 0U;
            EnterPumpChannelState(channel, PUMP_STATE_OFF);
        }
        break;

    case PUMP_STATE_FAULT:
    default:
        *pump_cmd = 0U;
        if ((g_alarm_latched == 0U) && (channel_fault_active == 0U))
        {
            EnterPumpChannelState(channel, PUMP_STATE_OFF);
        }
        break;
    }
}

static void RunSinglePumpLogic(void)
{
    uint8_t run_request = PumpRunRequest(g_in.pressure_p1, g_in.rpm_p1);
    uint8_t p1_not_ready_fault_active = ((g_fault_active_mask & FAULT_P1_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_not_ready_fault_new = ((g_fault_new_mask & FAULT_P1_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_pressure_timeout_active = ((g_fault_active_mask & FAULT_P1_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_pressure_timeout_new = ((g_fault_new_mask & FAULT_P1_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_fault_active = (uint8_t)(p1_not_ready_fault_active || p1_pressure_timeout_active);
    uint8_t p1_fault_new = (uint8_t)(p1_not_ready_fault_new || p1_pressure_timeout_new);

    if (g_in.ack_short)
    {
        if (g_state == PUMP_STATE_FAULT)
        {
            EnterState(PUMP_STATE_OFF);
        }
    }

    switch (g_state)
    {
    case PUMP_STATE_OFF:
        StopAllPumps();
        if (run_request)
        {
            if (p1_fault_active == 0U)
            {
                g_out.pump1_cmd = 1U;
                EnterState(PUMP_STATE_RUNNING);
            }
            else if (p1_fault_new != 0U)
            {
                EnterState(PUMP_STATE_FAULT);
            }
        }
        break;

    case PUMP_STATE_START_WAIT_AC:
    case PUMP_STATE_START_WAIT_RPM:
    case PUMP_STATE_RUN_WAIT_PRESSURE_RECOVERY:
    case PUMP_STATE_RUNNING:
        if (run_request)
        {
            if (p1_fault_active == 0U)
            {
                g_out.pump1_cmd = 1U;
                EnterState(PUMP_STATE_RUNNING);
            }
            else
            {
                StopAllPumps();
                if (p1_fault_new != 0U)
                {
                    EnterState(PUMP_STATE_FAULT);
                }
                else
                {
                    EnterState(PUMP_STATE_OFF);
                }
            }
        }
        else
        {
            StopAllPumps();
            EnterState(PUMP_STATE_OFF);
        }
        break;

    case PUMP_STATE_FAULT:
    default:
        StopAllPumps();
        if ((g_alarm_latched == 0U) && (p1_fault_active == 0U))
        {
            EnterState(PUMP_STATE_OFF);
        }
        break;
    }
}

static void RunDualPumpLogic(void)
{
    uint8_t selector_invalid_active = ((g_fault_active_mask & FAULT_SEL_INVALID_MASK) != 0U) ? 1U : 0U;
    uint8_t selector_invalid_new = ((g_fault_new_mask & FAULT_SEL_INVALID_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_not_ready_fault_active = ((g_fault_active_mask & FAULT_P1_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_not_ready_fault_new = ((g_fault_new_mask & FAULT_P1_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_not_ready_fault_active = ((g_fault_active_mask & FAULT_P2_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_not_ready_fault_new = ((g_fault_new_mask & FAULT_P2_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_pressure_timeout_active = ((g_fault_active_mask & FAULT_P1_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_pressure_timeout_new = ((g_fault_new_mask & FAULT_P1_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_pressure_timeout_active = ((g_fault_active_mask & FAULT_P2_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_pressure_timeout_new = ((g_fault_new_mask & FAULT_P2_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_fault_active = (uint8_t)(p1_not_ready_fault_active || p1_pressure_timeout_active);
    uint8_t p1_fault_new = (uint8_t)(p1_not_ready_fault_new || p1_pressure_timeout_new);
    uint8_t p2_fault_active = (uint8_t)(p2_not_ready_fault_active || p2_pressure_timeout_active);
    uint8_t p2_fault_new = (uint8_t)(p2_not_ready_fault_new || p2_pressure_timeout_new);

    if (g_in.ack_short)
    {
        if (g_state == PUMP_STATE_FAULT)
        {
            EnterState(PUMP_STATE_OFF);
        }
    }

    if (selector_invalid_active != 0U)
    {
        StopAllPumps();
        if (selector_invalid_new != 0U)
        {
            EnterState(PUMP_STATE_FAULT);
        }
    }

    if (g_in.selector == SELECTOR_OFF)
    {
        StopAllPumps();
        EnterState(PUMP_STATE_OFF);
    }

    if ((g_state != PUMP_STATE_OFF) &&
        (g_state != PUMP_STATE_FAULT) &&
        (g_in.selector != SELECTOR_OFF) &&
        (g_in.selector != SELECTOR_INVALID) &&
        (g_in.selector != g_active_pump))
    {
        StopAllPumps();
        EnterState(PUMP_STATE_OFF);
    }

    switch (g_state)
    {
    case PUMP_STATE_OFF:
        StopAllPumps();
        if (g_in.selector == SELECTOR_P1)
        {
            if (PumpRunRequest(g_in.pressure_p1, g_in.rpm_p1))
            {
                g_active_pump = SELECTOR_P1;
                if (p1_fault_active == 0U)
                {
                    g_out.pump1_cmd = 1U;
                    EnterState(PUMP_STATE_RUNNING);
                }
                else if (p1_fault_new != 0U)
                {
                    EnterState(PUMP_STATE_FAULT);
                }
            }
        }
        else if (g_in.selector == SELECTOR_P2)
        {
            if (PumpRunRequest(g_in.pressure_p2, g_in.rpm_p2))
            {
                g_active_pump = SELECTOR_P2;
                if (p2_fault_active == 0U)
                {
                    g_out.pump2_cmd = 1U;
                    EnterState(PUMP_STATE_RUNNING);
                }
                else if (p2_fault_new != 0U)
                {
                    EnterState(PUMP_STATE_FAULT);
                }
            }
        }
        break;

    case PUMP_STATE_START_WAIT_AC:
    case PUMP_STATE_START_WAIT_RPM:
    case PUMP_STATE_RUN_WAIT_PRESSURE_RECOVERY:
    case PUMP_STATE_RUNNING:
        if (SelectedPumpIsP1())
        {
            if (PumpRunRequest(g_in.pressure_p1, g_in.rpm_p1))
            {
                if (p1_fault_active == 0U)
                {
                    g_out.pump1_cmd = 1U;
                    g_out.pump2_cmd = 0U;
                    EnterState(PUMP_STATE_RUNNING);
                }
                else
                {
                    StopAllPumps();
                    if (p1_fault_new != 0U)
                    {
                        EnterState(PUMP_STATE_FAULT);
                    }
                    else
                    {
                        EnterState(PUMP_STATE_OFF);
                    }
                }
            }
            else
            {
                StopAllPumps();
                EnterState(PUMP_STATE_OFF);
            }
        }
        else if (SelectedPumpIsP2())
        {
            if (PumpRunRequest(g_in.pressure_p2, g_in.rpm_p2))
            {
                if (p2_fault_active == 0U)
                {
                    g_out.pump2_cmd = 1U;
                    g_out.pump1_cmd = 0U;
                    EnterState(PUMP_STATE_RUNNING);
                }
                else
                {
                    StopAllPumps();
                    if (p2_fault_new != 0U)
                    {
                        EnterState(PUMP_STATE_FAULT);
                    }
                    else
                    {
                        EnterState(PUMP_STATE_OFF);
                    }
                }
            }
            else
            {
                StopAllPumps();
                EnterState(PUMP_STATE_OFF);
            }
        }
        else
        {
            StopAllPumps();
            EnterState(PUMP_STATE_OFF);
        }
        break;

    case PUMP_STATE_FAULT:
    default:
        StopAllPumps();
        if ((g_alarm_latched == 0U) && (g_fault_active_mask == 0U))
        {
            EnterState(PUMP_STATE_OFF);
        }
        break;
    }

    if (g_out.pump1_cmd && g_out.pump2_cmd)
    {
        g_out.pump2_cmd = 0U;
    }
}

static void RunBypassDualLogic(void)
{
    uint8_t p1_not_ready_fault_active = ((g_fault_active_mask & FAULT_P1_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_not_ready_fault_new = ((g_fault_new_mask & FAULT_P1_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_not_ready_fault_active = ((g_fault_active_mask & FAULT_P2_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_not_ready_fault_new = ((g_fault_new_mask & FAULT_P2_NOT_READY_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_pressure_timeout_active = ((g_fault_active_mask & FAULT_P1_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_pressure_timeout_new = ((g_fault_new_mask & FAULT_P1_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_pressure_timeout_active = ((g_fault_active_mask & FAULT_P2_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p2_pressure_timeout_new = ((g_fault_new_mask & FAULT_P2_PRESSURE_TIMEOUT_MASK) != 0U) ? 1U : 0U;
    uint8_t p1_fault_active = (uint8_t)(p1_not_ready_fault_active || p1_pressure_timeout_active);
    uint8_t p1_fault_new = (uint8_t)(p1_not_ready_fault_new || p1_pressure_timeout_new);
    uint8_t p2_fault_active = (uint8_t)(p2_not_ready_fault_active || p2_pressure_timeout_active);
    uint8_t p2_fault_new = (uint8_t)(p2_not_ready_fault_new || p2_pressure_timeout_new);

    RunPumpChannel(&g_p1_channel,
                   g_in.pressure_p1,
                   p1_fault_active,
                   p1_fault_new,
                   g_in.rpm_p1,
                   &g_out.pump1_cmd);

    RunPumpChannel(&g_p2_channel,
                   g_in.pressure_p2,
                   p2_fault_active,
                   p2_fault_new,
                   g_in.rpm_p2,
                   &g_out.pump2_cmd);

}

static void RunOutputTestProgram(void)
{
    uint32_t now = HAL_GetTick();

    g_test_sys_led1 = 0U;
    g_test_sys_led2 = 0U;
    g_fault_active_mask = 0U;
    g_fault_new_mask = 0U;
    g_fault_prev_active_mask = 0U;
    g_fault_latched_mask = 0U;
    g_alarm_latched = 0U;

    if ((now - g_test_relay_tick) >= T_OUTPUT_TEST_STEP_MS)
    {
        g_test_relay_tick = now;
        g_test_relay_step = (uint8_t)((g_test_relay_step + 1U) % 4U);
    }

    switch (g_test_relay_step)
    {
    case 0:
        g_out.failure_ams = 1U;
        break;
    case 1:
        g_out.pump1_cmd = 1U;
        break;
    case 2:
        g_out.pump2_cmd = 1U;
        break;
    case 3:
    default:
        /* 200 ms break before the relay sequence repeats from Q1. */
        break;
    }

    if (g_in.pressure_p1)
    {
        g_out.ind1_system_ready = 1U;  /* I4 -> IND9 */
    }
    else if (g_in.rpm_p1)
    {
        g_out.ind2_p1_ready = 1U;      /* I5 -> IND1 */
    }
    else if (g_in.pressure_p2)
    {
        g_out.ind3_p1_on = 1U;         /* I6 -> IND10 */
    }
    else if (g_in.rpm_p2)
    {
        g_out.ind4_p1_standby = 1U;    /* I7 -> IND2 */
    }
    else
    {
        if ((now - g_test_led_tick) >= T_OUTPUT_TEST_STEP_MS)
        {
            g_test_led_tick = now;
            g_test_led_step = (uint8_t)((g_test_led_step + 1U) % 9U);
        }

        switch (g_test_led_step)
        {
        case 0:
            g_out.ind1_system_ready = 1U;  /* IND9  */
            break;
        case 1:
            g_out.ind2_p1_ready = 1U;      /* IND1  */
            break;
        case 2:
            g_out.ind3_p1_on = 1U;         /* IND10 */
            break;
        case 3:
            g_out.ind4_p1_standby = 1U;    /* IND2  */
            break;
        case 4:
            g_out.ind5_p2_ready = 1U;      /* IND11 */
            break;
        case 5:
            g_out.ind6_p2_on = 1U;         /* IND3  */
            break;
        case 6:
            g_out.ind7_p2_standby = 1U;    /* IND12 */
            break;
        case 7:
            g_out.ind8_pressure_low = 1U;  /* IND4  */
            break;
        case 8:
        default:
            g_out.ind9_standby_alarm = 1U; /* IND13 */
            break;
        }
    }
}

static void RunControlLogic(void)
{
    ClearOutputs();

#if (CONTROL_MODE == CONTROL_MODE_OUTPUT_TEST_CFG)
    UpdateAlarmLatch(0U);
    RunOutputTestProgram();
#elif (CONTROL_MODE == CONTROL_MODE_BYPASS_DUAL_CFG)
    UpdateAlarmLatch(ComputeBypassFaultMask());
    RunBypassDualLogic();
#elif (APP_MODE == APP_MODE_SINGLE_PUMP_CFG)
    UpdateAlarmLatch(ComputeSinglePumpFaultMask());
    RunSinglePumpLogic();
#else
    UpdateAlarmLatch(ComputeDualPumpFaultMask());
    RunDualPumpLogic();
#endif

    if (CONTROL_MODE != CONTROL_MODE_OUTPUT_TEST_CFG)
    {
        g_out.ind1_system_ready = (g_alarm_latched == 0U) ? 1U : 0U;
        g_out.ind2_p1_ready = P1Ready();
        g_out.ind3_p1_on = g_out.pump1_cmd;
        g_out.ind4_p1_standby =
#if (CONTROL_MODE == CONTROL_MODE_BYPASS_DUAL_CFG)
            0U;
#else
            (APP_MODE == APP_MODE_DUAL_PUMP_CFG) ? (g_in.selector == SELECTOR_P2) : 0U;
#endif
        g_out.ind5_p2_ready = P2Ready();
        g_out.ind6_p2_on = g_out.pump2_cmd;
        g_out.ind7_p2_standby =
#if (CONTROL_MODE == CONTROL_MODE_BYPASS_DUAL_CFG)
            0U;
#else
            (APP_MODE == APP_MODE_DUAL_PUMP_CFG) ? (g_in.selector == SELECTOR_P1) : 0U;
#endif
        g_out.ind8_pressure_low =
#if (CONTROL_MODE == CONTROL_MODE_BYPASS_DUAL_CFG)
            (uint8_t)(g_in.pressure_p1 || g_in.pressure_p2);
#else
            PressureDemandActive();
#endif
        g_out.ind9_standby_alarm =
            (((g_fault_latched_mask & (FAULT_P1_PRESSURE_TIMEOUT_MASK | FAULT_P2_PRESSURE_TIMEOUT_MASK)) != 0U) &&
             g_alarm_blink)
                ? 1U
                : 0U;

        if ((g_fault_latched_mask & (FAULT_P1_PRESSURE_TIMEOUT_MASK | FAULT_P2_PRESSURE_TIMEOUT_MASK)) != 0U)
        {
            g_out.failure_ams = 1U;
        }

        ApplyLampTestIfNeeded();
    }
}

static void ApplyLampTestIfNeeded(void)
{
    if (g_in.lamp_test)
    {
        g_out.ind1_system_ready = 1U;
        g_out.ind2_p1_ready = 1U;
        g_out.ind3_p1_on = 1U;
        g_out.ind4_p1_standby = 1U;
        g_out.ind5_p2_ready = 1U;
        g_out.ind6_p2_on = 1U;
        g_out.ind7_p2_standby = 1U;
        g_out.ind8_pressure_low = 1U;
        g_out.ind9_standby_alarm = 1U;
    }
}

static void UpdateOutputs(void)
{
    uint8_t relay_byte = 0U;
    uint8_t led_byte_1 = 0U; /* U3 = IND1..IND8 */
    uint8_t led_byte_2 = 0U; /* U6 = IND9..IND16 */

    /* New DC order:
       Q1 = Failure AMS
       Q2 = Pump 1 output
       Q3 = Pump 2 output
    */

    /* Relay board wiring is reversed: QA..QH drive Q8..Q1 through the ULN stage. */
    if (g_out.failure_ams)
        relay_byte |= (1U << 7); /* Q1 = Failure AMS */
    if (g_out.pump1_cmd)
        relay_byte |= (1U << 6); /* Q2 = Output pump 1 */
    if (g_out.pump2_cmd)
        relay_byte |= (1U << 5); /* Q3 = Output pump 2 */

    /* U3 : IND1..IND8 */
    if (g_out.ind2_p1_ready)
        led_byte_1 |= (1U << 0); /* IND1  */
    if (g_out.ind4_p1_standby)
        led_byte_1 |= (1U << 1); /* IND2  */
    if (g_out.ind6_p2_on)
        led_byte_1 |= (1U << 2); /* IND3  */
    if (g_out.ind8_pressure_low)
        led_byte_1 |= (1U << 3); /* IND4  */

    /* U6 : IND9..IND16 */
    if (g_out.ind1_system_ready)
        led_byte_2 |= (1U << 0); /* IND9  */
    if (g_out.ind3_p1_on)
        led_byte_2 |= (1U << 1); /* IND10 */
    if (g_out.ind5_p2_ready)
        led_byte_2 |= (1U << 2); /* IND11 */
    if (g_out.ind7_p2_standby)
        led_byte_2 |= (1U << 3); /* IND12 */
    if (g_out.ind9_standby_alarm)
        led_byte_2 |= (1U << 4); /* IND13 */

    if (g_in.lamp_test)
    {
        /* Drive all panel indicator bits directly during lamp test so the
           visible panel state does not depend on any intermediate logic. */
        led_byte_1 |= (1U << 0); /* IND1  */
        led_byte_1 |= (1U << 1); /* IND2  */
        led_byte_1 |= (1U << 2); /* IND3  */
        led_byte_1 |= (1U << 3); /* IND4  */

        led_byte_2 |= (1U << 0); /* IND9  */
        led_byte_2 |= (1U << 1); /* IND10 */
        led_byte_2 |= (1U << 2); /* IND11 */
        led_byte_2 |= (1U << 3); /* IND12 */
        led_byte_2 |= (1U << 4); /* IND13 */
    }

    SR_Write24(relay_byte, led_byte_1, led_byte_2);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

    ReadRawInputs();
    PrimeDebouncedInputs();
    ProcessInputs();
    SR_Write24(0U, 0U, 0U);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        ReadRawInputs();
        ProcessInputs();
        RunControlLogic();
        UpdateOutputs();
        UpdateSysLeds();
        HAL_Delay(LOOP_DELAY_MS);
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, SYS_LED1_Pin|SYS_LED2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SW_COMMON_GPIO_Port, SW_COMMON_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SR_LATCH_GPIO_Port, SR_LATCH_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SR_OE_GPIO_Port, SR_OE_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : SYS_LED1_Pin SYS_LED2_Pin */
  GPIO_InitStruct.Pin = SYS_LED1_Pin|SYS_LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : SW_COMMON_Pin */
  GPIO_InitStruct.Pin = SW_COMMON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SW_COMMON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SR_LATCH_Pin SR_OE_Pin */
  GPIO_InitStruct.Pin = SR_LATCH_Pin|SR_OE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : SEL_P1_Pin SEL_P2_Pin */
  GPIO_InitStruct.Pin = SEL_P1_Pin|SEL_P2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : I1_Pin I2_Pin */
  GPIO_InitStruct.Pin = I1_Pin|I2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : I3_Pin */
  GPIO_InitStruct.Pin = I3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(I3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : I4_Pin I5_Pin I6_Pin I7_Pin
                           ACK_LT_Pin AC1_IN_Pin AC2_IN_Pin */
  GPIO_InitStruct.Pin = I4_Pin|I5_Pin|I6_Pin|I7_Pin
                          |ACK_LT_Pin|AC1_IN_Pin|AC2_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
