#pragma once

// ================== КОНФИГУРАЦИЯ СНИФЕРА ШЛАГБАУМА ==================
// CYD ESP32-2432S028R + CC1101 (433 МГц)
//
// Распиновка CYD:
//   HSPI (дисплей ILI9341): SCK=14, MOSI=13, MISO=12, CS=15, DC=2, BL=21
//   VSPI (тач XPT2046):     CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
//   CC1101 припаян к площадкам SD-слота (обратная сторона платы):
//     SCK=18 (SD CLK), MISO=19 (SD D0), MOSI=23 (SD CMD)
//   ВАЖНО: SD-карту при этом НЕ вставлять — её сигналы повесятся на ту же шину!
//   CC1101 живёт на тех же 18/19/23 (VSPI): перед радиооперацией мультиплексор
//   пинов переключается на радио, после — обратно на тач (radio_driver.cpp).

#define CC1101_SCK    18   // SD CLK (пайка к площадке слота)
#define CC1101_MISO   19   // SD D0
#define CC1101_MOSI   23   // SD CMD
#define CC1101_CS     22   // свободный пин CYD (P3/CN1)
#define CC1101_GDO0   27   // свободный пин CYD (CN1)

// Пины тача — для обратного переключения мультиплексора VSPI
#define TOUCH_SCK     25
#define TOUCH_MISO    39
#define TOUCH_MOSI    32

// Радио
#define BARRIER_FREQ_MHZ  433.92f  // частота по умолчанию

// Параметры захвата
#define MAX_PULSES        400      // максимум импульсов в буфере захвата
#define MIN_PULSE_US      20       // фильтр глитчей (микросекунды)
#define SCAN_TIMEOUT_MS    15000   // общее время ожидания сигнала в сканере
#define CAPTURE_ROUND_MS   250     // раунд RMT-ожидания между опросами тача

// Память (NVS): 8 слотов-пресетов + текущая частота
#define SLOT_COUNT        8
#define NVS_NAMESPACE     "sniffer"
#define NVS_KEY_CUR_FREQ  "cur_freq"
// Старые ключи одной ячейки (v1.0.x) — мигрируются в слот 0 при старте
#define NVS_OLD_KEY_FREQ  "freq_mhz"
#define NVS_OLD_KEY_LEN   "pulses_len"
#define NVS_OLD_KEY_DATA  "pulses"
#define NVS_OLD_KEY_MOD   "modulation"
#define NVS_OLD_KEY_LEVELS "levels"