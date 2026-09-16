#pragma once

// ================== КОНФИГУРАЦИЯ ПРОТОТИПА СНИФЕРА ШЛАГБАУМА ==================
// CYD ESP32-2432S028R + CC1101 (433 МГц)
//
// Распиновка CYD:
//   HSPI (дисплей ILI9341): SCK=14, MOSI=13, MISO=12, CS=15, DC=2, BL=21
//   VSPI (тач XPT2046):     CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
//   CC1101 припаян к площадкам SD-слота (обратная сторона платы):
//     SCK=18 (SD CLK), MISO=19 (SD D0), MOSI=23 (SD CMD)
//   ВАЖНО: SD-карту при этом НЕ вставлять — её сигналы повесятся на ту же шину!
//   Контроллеров SPI на ESP32 два (HSPI=дисплей, VSPI=тач), третьего нет.
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
#define BARRIER_FREQ_MHZ  433.92f  // типичная частота шлагбаумов; правится на экране

// Параметры захвата
#define MAX_PULSES        400      // максимум импульсов в буфере захвата
#define MIN_PULSE_US      20       // фильтр глитчей (микросекунды)
#define CAPTURE_TIMEOUT_MS 5000    // сколько ждём сигнал в режиме снифера
#define MAX_PULSE_US      32767    // ограничение rmt_data_t (15 бит)

// Память (NVS)
#define NVS_NAMESPACE     "sniffer"
#define NVS_KEY_FREQ      "freq_mhz"
#define NVS_KEY_LEN       "pulses_len"
#define NVS_KEY_DATA      "pulses"
#define NVS_KEY_MOD       "modulation"
#define NVS_KEY_LEVELS    "levels"