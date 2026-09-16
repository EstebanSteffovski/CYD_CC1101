#include "ui_manager.h"

UIManager ui;

// Раскладка кнопок меню: 3 вертикальные кнопки
#define BTN_X        20
#define BTN_W        200
#define BTN_H        60
#define BTN_GAP      18
#define BTN_Y0       70

// Кнопки экрана результата
#define RES_BTN_Y    235
#define RES_BTN_H    55

void UIManager::setup() {
    tft.begin();
    tft.setRotation(0);   // портрет 240x320, как в CAN-снифере
    tft.fillScreen(COL_BG);

    u8g2.begin(tft);

    // Тач — на ГЛОБАЛЬНОМ SPI (тот же объект, что и CC1101).
    // На ESP32 два SPI-периферийных блока (HSPI=дисплей, VSPI=тач/CC1101).
    // Если дать тачу отдельный SPIClass(VSPI), радио на глобальном SPI
    // потеряет хост — CC1101 перестанет отвечать. Один объект = честный
    // мультиплексор end/begin в radio_driver.cpp.
    touch = new XPT2046_Touchscreen(33, 36);
    touch->begin(SPI);
    touch->setRotation(0);

    drawMenu();
}

// ================== КНОПКА ==================

void UIManager::drawButton(int x, int y, int w, int h, const char* label, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, 8, color);
    tft.drawRoundRect(x, y, w, h, 8, TFT_WHITE);
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(TFT_BLACK);
    u8g2.setBackgroundColor(color);
    u8g2.setCursor(x + w / 2 - u8g2.getUTF8Width(label) / 2, y + h / 2 + 7);
    u8g2.print(label);
}

// ================== МЕНЮ (3 КНОПКИ) ==================

void UIManager::drawMenu() {
    tft.fillScreen(COL_BG);
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(COL_ACCENT);
    u8g2.setBackgroundColor(COL_BG);
    u8g2.setCursor(14, 34);
    u8g2.print("СНИФЕР ШЛАГБАУМА");

    drawButton(BTN_X, BTN_Y0, BTN_W, BTN_H, "1. Снифер", COL_ACCENT);
    drawButton(BTN_X, BTN_Y0 + (BTN_H + BTN_GAP), BTN_W, BTN_H, "2. Записать", COL_OK);
    drawButton(BTN_X, BTN_Y0 + 2 * (BTN_H + BTN_GAP), BTN_W, BTN_H, "3. Воспроизв.", COL_WARN);

    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.setForegroundColor(COL_DIM);
    char buf[48];
    snprintf(buf, sizeof(buf), "Частота: %.2f МГц", displayFreq);
    u8g2.setCursor(14, 316);
    u8g2.print(buf);
}

// ================== ЭКРАН СНИФЕРА ==================

void UIManager::drawSniff() {
    tft.fillScreen(COL_BG);
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(COL_ACCENT);
    u8g2.setBackgroundColor(COL_BG);
    u8g2.setCursor(20, 40);
    u8g2.print("РЕЖИМ СНИФЕРА");

    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.setForegroundColor(COL_TEXT);
    u8g2.setCursor(20, 75);
    u8g2.print("Нажми кнопку пульта");

    // Таймер
    u8g2.setForegroundColor(COL_DIM);
    uint32_t sec = (millis() - sniffStartTime) / 1000;
    if (sec > 999) sec = 999;
    char buf[32];
    snprintf(buf, sizeof(buf), "Ожидание: %u с", (unsigned)sec);
    u8g2.setCursor(20, 100);
    u8g2.print(buf);

    // Мигающий индикатор приёма
    static uint32_t lastDot = 0;
    static bool dotOn = false;
    if (millis() - lastDot > 400) {
        lastDot = millis();
        dotOn = !dotOn;
        tft.fillCircle(218, 22, 6, dotOn ? COL_ERR : COL_BG);
    }

    // Кнопка "Стоп"
    drawButton(BTN_X, 230, BTN_W, BTN_H, "Стоп / Меню", COL_DIM);
}

// ================== ЭКРАН РЕЗУЛЬТАТА ==================

void UIManager::drawResult() {
    tft.fillScreen(COL_BG);
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(sniffSuccess ? COL_OK : COL_ERR);
    u8g2.setBackgroundColor(COL_BG);
    u8g2.setCursor(20, 40);
    u8g2.print(sniffSuccess ? "СИГНАЛ ПОЙМАН" : "НИЧЕГО НЕ ПОЙМАНО");

    if (sniffSuccess) {
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setForegroundColor(COL_TEXT);
        char buf[48];
        snprintf(buf, sizeof(buf), "Частота: %.2f МГц", sniffFreq);
        u8g2.setCursor(20, 72);
        u8g2.print(buf);

        snprintf(buf, sizeof(buf), "Импульсов: %d", sniffPulses);
        u8g2.setCursor(20, 90);
        u8g2.print(buf);

        snprintf(buf, sizeof(buf), "RSSI: %d dBm", sniffRssi);
        u8g2.setCursor(20, 108);
        u8g2.print(buf);

        // Мини-осциллограмма первых импульсов
        int x = 20;
        int base = 190;
        int scale = 2;   // 1 пиксель = 10 мкс
        for (int i = 0; i + 1 < previewCount && x < 218; i++) {
            uint16_t dur = previewPulses[i] / 10;
            if (dur < 2) dur = 2;
            if (dur > 60) dur = 60;
            uint16_t level = (i % 2 == 0) ? base - dur : base;
            tft.drawLine(x, base, x + 2, level, COL_ACCENT);
            x += 3;
        }
    } else {
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setForegroundColor(COL_DIM);
        u8g2.setCursor(20, 80);
        u8g2.print("Жми кнопку пульта");
        u8g2.setCursor(20, 96);
        u8g2.print("ближе к антенне");
    }

    // Всплывающие статусы
    if (millis() < saveFlashUntil) {
        u8g2.setFont(u8g2_font_10x20_t_cyrillic);
        u8g2.setForegroundColor(COL_OK);
        u8g2.setCursor(20, 145);
        u8g2.print("СОХРАНЕНО");
    }
    if (millis() < replayFlashUntil) {
        u8g2.setFont(u8g2_font_10x20_t_cyrillic);
        u8g2.setForegroundColor(COL_WARN);
        u8g2.setCursor(20, 175);
        u8g2.print("ОТПРАВЛЕНО");
    }

    // Кнопки: Запись | Воспр. | Меню
    drawButton(15, RES_BTN_Y, 100, RES_BTN_H, "Запись", COL_OK);
    drawButton(125, RES_BTN_Y, 100, RES_BTN_H, "Воспр.", COL_WARN);
    drawButton(75, RES_BTN_Y + RES_BTN_H + 10, 90, 30, "Меню", COL_DIM);
}

// ================== НАВИГАЦИЯ ==================

void UIManager::gotoResult() {
    currentScreen = SCREEN_RESULT;
    redraw = true;
}

void UIManager::gotoMenu() {
    currentScreen = SCREEN_MENU;
    redraw = true;
}

// ================== ТАЧ ==================

void UIManager::handleTouch(int16_t tx, int16_t ty) {
    if (currentScreen == SCREEN_MENU) {
        // Кнопка 1: снифер
        if (tx >= BTN_X && tx <= BTN_X + BTN_W && ty >= BTN_Y0 && ty <= BTN_Y0 + BTN_H) {
            actionSniff = true;
            sniffStartTime = millis();
            currentScreen = SCREEN_SNIFF;
            redraw = true;
        }
        // Кнопка 2: запись
        else if (tx >= BTN_X && tx <= BTN_X + BTN_W &&
                 ty >= BTN_Y0 + (BTN_H + BTN_GAP) && ty <= BTN_Y0 + (BTN_H + BTN_GAP) + BTN_H) {
            actionSave = true;
        }
        // Кнопка 3: воспроизведение
        else if (tx >= BTN_X && tx <= BTN_X + BTN_W &&
                 ty >= BTN_Y0 + 2 * (BTN_H + BTN_GAP) && ty <= BTN_Y0 + 2 * (BTN_H + BTN_GAP) + BTN_H) {
            actionReplay = true;
        }
    }
    else if (currentScreen == SCREEN_SNIFF) {
        // Кнопка "Стоп" — снифер сам блокирует loop, но если не блокирует — выходим
        if (tx >= BTN_X && tx <= BTN_X + BTN_W && ty >= 230 && ty <= 230 + BTN_H) {
            gotoMenu();
        }
    }
    else if (currentScreen == SCREEN_RESULT) {
        if (ty >= RES_BTN_Y && ty <= RES_BTN_Y + RES_BTN_H) {
            if (tx >= 15 && tx <= 115)       actionSave = true;
            else if (tx >= 115 && tx <= 225) actionReplay = true;
        }
        else if (ty >= RES_BTN_Y + RES_BTN_H + 10 && ty <= RES_BTN_Y + RES_BTN_H + 40) {
            if (tx >= 75 && tx <= 165) gotoMenu();
        }
    }
}

void UIManager::loop() {
    if (redraw) {
        switch (currentScreen) {
            case SCREEN_MENU:   drawMenu();   break;
            case SCREEN_SNIFF:  drawSniff();  break;
            case SCREEN_RESULT: drawResult(); break;
        }
        redraw = false;
    }

    // Обновление всплывающих статусов (если истекли — перерисовать)
    static bool wasSave = false, wasReplay = false;
    bool nowSave = millis() < saveFlashUntil;
    bool nowReplay = millis() < replayFlashUntil;
    if ((wasSave && !nowSave) || (wasReplay && !nowReplay)) {
        redraw = true;
    }
    wasSave = nowSave;
    wasReplay = nowReplay;

    // Секундный таймер на экране снифера — обновление
    if (currentScreen == SCREEN_SNIFF) {
        static uint32_t lastSec = 0;
        uint32_t sec = (millis() - sniffStartTime) / 1000;
        if (sec != lastSec) {
            lastSec = sec;
            redraw = true;
        }
    }

    // Тач
    if (touch && touch->tirqTouched() && touch->touched()) {
        if (millis() - lastTouch < 300) return;
        lastTouch = millis();
        TS_Point p = touch->getPoint();
        int16_t tx = map(p.x, 200, 3700, 0, 240);
        int16_t ty = map(p.y, 240, 3800, 0, 320);
        if (tx < 0) tx = 0;
        if (tx > 239) tx = 239;
        if (ty < 0) ty = 0;
        if (ty > 319) ty = 319;
        handleTouch(tx, ty);
    }
}