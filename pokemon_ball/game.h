/*
 * game.h — Game state machine.
 *
 * Coordinates the four independent modules (display, gesture, touch, mqtt)
 * through a simple non-blocking state machine:
 *
 *   IDLE          → pokeball on screen; poll for shake
 *   INFERING      → 2 s blocking inference; store gesture (not confirmed yet)
 *   GESTURE_READY → pokeball with "TOUCH!" hint; wait for tap
 *   REVEALED      → pokemon (or hidden Sanjini) on screen; auto-return to IDLE
 */
#pragma once
#include <Arduino.h>

void gameInit();
void gameLoop();
