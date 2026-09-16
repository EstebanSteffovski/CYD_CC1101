#pragma once

// ================== КОНФИГУРАЦИЯ ПРОТОТИПА СНИФЕРА ШЛАГБАУМА ==================
// CYD ESP32-2432S028R + CC1101 (433 МГц)
//
// Распиновка CYD:
//   HSPI (дисплей ILI9341): SCK=14, MOSI=13, MISO=12, CS=15, DC=2, BL=21
//   VSPI (тач XPT2046):     CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
//   VSPI делится с CC1101:  SCK=25, MOSI=32, MISO=39 (параллельно тачу)
//   Свободные пины CYD:     CS_CC1101=22 (P3/CN1), GDO0=27 (CN1)
//   Не используется: GDO2 (не нужен, снифер на GDO0)

#define CC1101_SCK    25   // общий с тачем
#define CC1101_MISO   39   // общий с тачем
#define CC1101_MOSI   32   // общий с тачем
#define CC1101_CS     22   // свободный пин CYD (P3/CN1)
#define CC1101_GDO0   27   // свободный пин CYD (CN1)

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