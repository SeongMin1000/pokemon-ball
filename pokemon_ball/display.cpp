/*
 * display.cpp — Round LCD (GC9A01) display module implementation.
 *
 * Reuses the JPEG-to-TFT rendering pattern from TFT_flash_jpg_ex and the
 * TFT_eSPI initialisation from TFT_eSPI_Clock_ex2.  The pokeball is drawn
 * procedurally (no image asset needed) so the default screen works with
 * zero external resources.
 */
#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <JPEGDecoder.h>

static TFT_eSPI tft = TFT_eSPI();

#define minimum(a, b) (((a) < (b)) ? (a) : (b))

// Forward declarations (drawJpegCentered is defined below drawPokeball).
static void drawJpegCentered(const uint8_t* jpg, uint32_t sz);

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

// Draw the pokeball.  Uses the JPEG asset from config.h; falls back to
// the procedural drawing if no image is available.
// ready = true  → "TOUCH!" hint overlay (gesture already inferred)
// ready = false → plain idle pokeball
static void drawPokeball(bool ready) {
    const uint8_t* img = POKEBALL_IMAGE;
    if (img) {
        tft.fillScreen(TFT_BLACK);
        drawJpegCentered(img, (uint32_t)POKEBALL_IMAGE_SIZE);
    } else {
        tft.fillScreen(TFT_BLACK);

        // Top half red, bottom half white
        int half = SCREEN_H / 2;
        tft.fillRect(0, 0, SCREEN_W, half, TFT_RED);
        tft.fillRect(0, half, SCREEN_W, half, TFT_WHITE);

        // Black equator band
        int bandH = 12;
        int bandY = half - bandH / 2;
        tft.fillRect(0, bandY, SCREEN_W, bandH, TFT_BLACK);

        // Centre button
        int r1 = 28, r2 = 18;
        tft.fillCircle(CENTER_X, CENTER_Y, r1, TFT_BLACK);
        tft.fillCircle(CENTER_X, CENTER_Y, r2, TFT_WHITE);
        tft.drawCircle(CENTER_X, CENTER_Y, r2, TFT_BLACK);
    }

    if (ready) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.drawString("TOUCH!", CENTER_X, 20);
        tft.drawString("TO THROW", CENTER_X, SCREEN_H - 20);
    }
}

// Draw a coloured placeholder circle with the name centred.
static void drawPlaceholder(const char* name, uint16_t color) {
    tft.fillScreen(TFT_BLACK);
    tft.fillCircle(CENTER_X, CENTER_Y, 100, color);
    tft.drawCircle(CENTER_X, CENTER_Y, 100, TFT_WHITE);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, color);
    tft.setTextSize(2);
    // Draw name slightly below centre so it fits nicely inside the circle
    tft.drawString(name, CENTER_X, CENTER_Y);
}

// Decode a PROGMEM JPEG array directly to the TFT at (xpos, ypos).
// Mirrors the proven renderJPEG() from TFT_flash_jpg_ex.ino.
static void drawJpeg(const uint8_t arrayname[], uint32_t array_size,
                     int xpos, int ypos) {
    JpegDec.decodeArray(arrayname, array_size);

    uint16_t* pImg;
    uint16_t  mcu_w = JpegDec.MCUWidth;
    uint16_t  mcu_h = JpegDec.MCUHeight;
    uint32_t  max_x = JpegDec.width  + xpos;
    uint32_t  max_y = JpegDec.height + ypos;
    uint32_t  min_w = minimum(mcu_w, max_x % mcu_w);
    uint32_t  min_h = minimum(mcu_h, max_y % mcu_h);
    uint32_t  win_w = mcu_w;
    uint32_t  win_h = mcu_h;

    while (JpegDec.readSwappedBytes()) {
        pImg = JpegDec.pImage;
        int mcu_x = JpegDec.MCUx * mcu_w + xpos;
        int mcu_y = JpegDec.MCUy * mcu_h + ypos;

        if (mcu_x + mcu_w <= max_x) win_w = mcu_w; else win_w = min_w;
        if (mcu_y + mcu_h <= max_y) win_h = mcu_h; else win_h = min_h;

        if (win_w != mcu_w) {
            uint16_t* cImg = pImg + win_w;
            int p = 0;
            for (int h = 1; h < (int)win_h; h++) {
                p += mcu_w;
                for (int w = 0; w < (int)win_w; w++) {
                    *cImg = *(pImg + w + p);
                    cImg++;
                }
            }
        }

        if ((mcu_x + (int)win_w) <= tft.width() &&
            (mcu_y + (int)win_h) <= tft.height()) {
            tft.pushRect(mcu_x, mcu_y, win_w, win_h, pImg);
        } else if ((mcu_y + (int)win_h) >= tft.height()) {
            JpegDec.abort();
        }
    }
}

// Centre a JPEG on the 240×240 screen.
static void drawJpegCentered(const uint8_t* jpg, uint32_t sz) {
    // We need the image dimensions first — decode once to peek.
    JpegDec.decodeArray(jpg, sz);
    int x = (SCREEN_W - (int)JpegDec.width)  / 2;
    int y = (SCREEN_H - (int)JpegDec.height) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    // decodeArray consumed the data; re-decode for actual rendering.
    drawJpeg(jpg, sz, x, y);
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool displayBegin() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // Turn on backlight (TFT_BL = GPIO43, active HIGH)
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    displayPokeball(false);
    return true;
}

void displayPokeball(bool ready) {
    drawPokeball(ready);
}

void displayPokemon(const char* name, const uint8_t* jpg, uint32_t jpgSize,
                    uint16_t placeholderColor) {
    if (jpg && jpgSize > 0) {
        tft.fillScreen(TFT_BLACK);
        drawJpegCentered(jpg, jpgSize);
    } else {
        drawPlaceholder(name, placeholderColor);
    }
}

void displayHidden(const char* name, const uint8_t* jpg, uint32_t jpgSize) {
    // Hidden character gets a distinctive purple/gold placeholder.
    if (jpg && jpgSize > 0) {
        tft.fillScreen(TFT_BLACK);
        drawJpegCentered(jpg, jpgSize);
    } else {
        drawPlaceholder(name, TFT_PURPLE);
    }
}

void displayMessage(const char* line1, const char* line2) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    if (line1) tft.drawString(line1, CENTER_X, CENTER_Y - 15);
    if (line2) tft.drawString(line2, CENTER_X, CENTER_Y + 15);
}
