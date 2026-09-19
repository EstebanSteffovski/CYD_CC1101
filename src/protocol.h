#pragma once
#include <Arduino.h>
#include <vector>

// ============ ДЕКОДИРОВАНИЕ OOK-ПОСЫЛОК ============
// Распознаёт Princeton/EV1527/PT2262-семейство по кратности длительностей
// к базовому TE. Всё остальное честно зовётся RAW.

struct ProtocolInfo {
    bool valid = false;        // посылка распознана
    char name[24] = {0};       // "EV1527", "PT2262" и т.п.
    uint16_t teUs = 0;         // базовое время TE, мкс
    uint8_t bits[64] = {0};    // символы: 0, 1 или 2 (= 'f', три-стейт)
    uint8_t bitCount = 0;      // сколько символов декодировано
    uint32_t code = 0;         // первые 32 бита как число (для чисто бинарных)
    bool triState = false;     // есть три-стейт символы
};

// Возвращает распознанную посылку; valid=false => RAW (teUs всё равно заполнен)
ProtocolInfo decodeProtocol(const std::vector<uint16_t>& pulses,
                            const std::vector<uint8_t>& levels);

// Код в строку: 6/8 hex-цифр для бинарных, 'f' для три-стейта, '?' для каши
void formatCode(const ProtocolInfo& p, char* out, size_t outLen);