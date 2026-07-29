/*
 * touch.cpp — ESP32-S3 capacitive touch sensor with debounce.
 *
 * On the XIAO ESP32S3 the raw touch value RISES when the pad is touched
 * (baseline ~17500, touched > 40000).  This module detects a tap as a
 * rising-edge above TOUCH_THRESHOLD followed by a fall below it, with a
 * minimum DEBOUNCE_MS gap between consecutive taps.
 */
#include "touch.h"
#include "config.h"

static bool     wasTouched = false;
static uint32_t lastTapMs  = 0;

void touchBegin() {
    // ESP32 touch pins need no pinMode setup; touchRead() configures them.
    wasTouched = false;
    lastTapMs  = 0;
}

bool touchTapped() {
    uint32_t v = touchRead(TOUCH_PIN);
    bool touched = (v > TOUCH_THRESHOLD);

    // Rising edge → register a tap (if debounce window has passed).
    if (touched && !wasTouched) {
        uint32_t now = millis();
        if (now - lastTapMs >= DEBOUNCE_MS) {
            lastTapMs  = now;
            wasTouched = true;
            return true;
        }
    }
    wasTouched = touched;
    return false;
}
