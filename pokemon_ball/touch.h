/*
 * touch.h — Capacitive touch input module.
 *
 * Wraps touchRead() with debouncing so the caller simply gets a
 * rising-edge "the user just tapped" event.
 */
#pragma once
#include <Arduino.h>

void touchBegin();

// Returns true once per physical tap (debounced).
bool touchTapped();
