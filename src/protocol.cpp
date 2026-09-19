#include "protocol.h"
#include <math.h>
#include <algorithm>

// ============ ДЕКОДИРОВАНИЕ OOK-ПОСЫЛОК ============
// Вход: pulses+levels, нормализованные с HIGH (как их готовит radio_driver).
// Схема Princeton/EV1527/PT2262: каждый символ = пара (HIGH, LOW), длительности
// кратны TE: '0' = H1+L3, '1' = H3+L1, три-стейт 'f' = H1+L1. Между фреймами —
// sync-пауза LOW длиной 12+ TE (её пропускаем, фреймы повторяются).

ProtocolInfo decodeProtocol(const std::vector<uint16_t>& pulses,
                            const std::vector<uint8_t>& levels) {
    ProtocolInfo out;
    size_t n = std::min(pulses.size(), levels.size());
    if (n < 8) return out;

    // TE — медиана всех длительностей: большинство импульсов посылки = TE
    // (у '0' это HIGH, у '1' — LOW, у sync — HIGH), медиана устойчива к выбросам.
    std::vector<uint16_t> all(pulses.begin(), pulses.begin() + n);
    std::sort(all.begin(), all.end());
    uint16_t te = all[all.size() / 2];
    out.teUs = te;
    if (te < 50) return out;   // короче 50 мкс — мусор, не наш клиент

    size_t i = 0;
    if (levels[0] == 0) i = 1;   // подстраховка: начинаем с HIGH

    uint8_t bits[64];
    uint8_t bitCount = 0;
    bool sawBinary = false, sawTri = false;

    for (; i + 1 < n && bitCount < 64; i += 2) {
        uint16_t H = pulses[i], L = pulses[i + 1];
        int rH = (int)lroundf(H / (float)te);
        int rL = (int)lroundf(L / (float)te);
        if (rH < 1 || rH > 3) break;
        if (abs((int)H - rH * (int)te) > (int)(te * 0.45f)) break;
        if (rL >= 12) {            // sync-пауза между фреймами — пропускаем
            continue;
        }
        if (rL < 1 || rL > 3) break;
        if (abs((int)L - rL * (int)te) > (int)(te * 0.45f)) break;

        if (rH == 1 && rL == 3)      { bits[bitCount++] = 0; sawBinary = true; }
        else if (rH == 3 && rL == 1) { bits[bitCount++] = 1; sawBinary = true; }
        else if (rH == 1 && rL == 1) { bits[bitCount++] = 2; sawTri = true; }
        else break;                  // (2,2)/(3,3)/(3,2) — не семейство 2262
    }

    if (bitCount < 12) return out;   // короткое/неструктурное — RAW

    out.valid = true;
    out.bitCount = bitCount;
    memcpy(out.bits, bits, bitCount);
    out.triState = sawTri && !sawBinary ? true : (sawTri && sawBinary);

    uint32_t code = 0;
    for (int b = 0; b < bitCount && b < 32; b++) {
        code = (code << 1) | (bits[b] == 1 ? 1u : 0u);
    }
    out.code = code;

    bool pureBin = !sawTri;
    if (pureBin && bitCount % 24 == 0)      snprintf(out.name, sizeof(out.name), "EV1527");
    else if (pureBin && bitCount % 12 == 0) snprintf(out.name, sizeof(out.name), "PT2262/CAME");
    else if (sawTri)                        snprintf(out.name, sizeof(out.name), "PT2262 (3-стейт)");
    else                                    snprintf(out.name, sizeof(out.name), "Бинарный код");
    return out;
}

void formatCode(const ProtocolInfo& p, char* out, size_t outLen) {
    if (!p.valid || p.bitCount == 0) { snprintf(out, outLen, "-"); return; }
    // Кодируем первыми min(bitCount,32) символами, по 2 символа на hex-цифру.
    // '2' (три-стейт) -> 'f'; смешанная пара -> '?'.
    size_t chars = std::min<size_t>(p.bitCount, 32) / 2;
    if (chars == 0) { snprintf(out, outLen, "-"); return; }
    for (size_t c = 0; c < chars && c * 2 + 1 < p.bitCount && (2 * c + 1) < outLen; c++) {
        uint8_t hi = p.bits[2 * c], lo = p.bits[2 * c + 1];
        uint8_t val;
        if (hi == 2 && lo == 2)      val = 0xF;
        else if (hi != 2 && lo != 2) val = (hi << 1) | lo;
        else                         val = 0xFF;   // '?'
        if (val == 0xFF) out[c] = '?';
        else snprintf(out + c, 2, "%X", val);
    }
    out[chars] = 0;
}