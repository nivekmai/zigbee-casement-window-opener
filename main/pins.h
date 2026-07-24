#pragma once
#include "driver/gpio.h"

/*
 * Seeed Studio XIAO ESP32-C6 labels:
 * D0=GPIO0, D1=GPIO1, D2=GPIO2, D3=GPIO21,
 * D4=GPIO22, D5=GPIO23, D6=GPIO16.
 */
#define PIN_MOTOR_IN1       GPIO_NUM_0   // XIAO D0
#define PIN_MOTOR_IN2       GPIO_NUM_1   // XIAO D1
#define PIN_ENCODER_A       GPIO_NUM_2   // XIAO D2
#define PIN_ENCODER_B       GPIO_NUM_21  // XIAO D3
#define PIN_BUTTON_OPEN     GPIO_NUM_22  // XIAO D4
#define PIN_BUTTON_CLOSE    GPIO_NUM_23  // XIAO D5
#define PIN_SLIP_SWITCH     GPIO_NUM_16  // XIAO D6

/* Set to 1 if IN1=1/IN2=0 physically closes instead of opens. */
#define MOTOR_DIRECTION_INVERTED 0
