/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Copyright 2018 Gal Zaidenstein.
 */

#ifndef KEYPRESS_HANDLES_C
#define KEYPRESS_HANDLES_C
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "keymap.c"
#include "matrix.h"
#include "hal_ble.h"
#include "keycode_conv.h"
#include "oled_tasks.h"
#include "nvs_keymaps.h"
#include "nvs_funcs.h"
#include "plugin_manager.h"
#include "rgb_led.h"
#include "gesture_handles.h"

#define KEY_PRESS_TAG "KEY_PRESS"

/*
 * Current state of the keymap,each cell will hold the location of the key in the key report,
 *if a key is not in the report it will be set to 0
 */

uint8_t KEY_STATE[MATRIX_ROWS][KEYMAP_COLS] = {0};
uint16_t led_status = 0;
uint8_t modifier = 0;
uint16_t keycode = 0;

// Sizing the report for N-key rollover
uint8_t current_report[REPORT_LEN] = {0};

// Array to send when releasing macros
uint8_t macro_release[3] = {0};

// Flag in order to know when to ignore layer change on layer hold
uint8_t layer_hold_flag = 0;
uint8_t prev_layout = 0;

// checking if a modifier key was pressed
uint16_t check_modifier(uint16_t key)
{

	uint8_t cur_mod = 0;
	// these are the modifier keys
	if ((KC_LCTRL <= key) && (key <= KC_RGUI))
	{
		cur_mod = (1 << (key - KC_LCTRL));
		return cur_mod;
	}
	return 0;
}

// checking if a led status key was pressed, returning current led status
uint16_t check_led_status(uint16_t key)
{

	switch (key)
	{
	case KC_NLCK:
		return 1;

	case KC_CAPS:
		return 2;

	case KC_SLCK:
		return 3;
	}
	return 0;
}

// what to do on a media key press
void media_control_send(uint16_t keycode)
{

	uint8_t media_state[2] = {0};
	if (keycode == KC_MEDIA_NEXT_TRACK)
	{
		media_state[1] = 10;
	}
	if (keycode == KC_MEDIA_PREV_TRACK)
	{
		media_state[1] = 11;
	}
	if (keycode == KC_MEDIA_STOP)
	{
		media_state[1] = 12;
	}
	if (keycode == KC_MEDIA_PLAY_PAUSE)
	{
		media_state[1] = 5;
	}
	if (keycode == KC_AUDIO_MUTE)
	{
		media_state[1] = 1;
	}
	if (keycode == KC_AUDIO_VOL_UP)
	{
		SET_BIT(media_state[0], 6);
	}
	if (keycode == KC_AUDIO_VOL_DOWN)
	{
		SET_BIT(media_state[0], 7);
	}

	xQueueSend(media_q, (void *)&media_state, (TickType_t)0);
}

void media_control_release(uint16_t keycode)
{
	uint8_t media_state[2] = {0};
	xQueueSend(media_q, (void *)&media_state, (TickType_t)0);
}

// Send a macro as a sequence of steps. Steps are groups of up to MACRO_LEN keycodes
// separated by KC_NO mid-array. Each step is press+release with MACRO_STEP_DELAY_MS between.
static void send_macro_sequence(uint8_t macro_idx)
{
	uint8_t step_report[REPORT_LEN] = {0};
	uint8_t release_report[REPORT_LEN] = {0};
	uint16_t *keys = user_macros[macro_idx].key;

	int pos = 0;
	while (pos < USER_MACRO_LEN)
	{
		if (keys[pos] == KC_NO)
		{
			// trailing terminator — end of macro
			break;
		}

		// collect one step (up to MACRO_LEN keys, until KC_NO or end)
		memset(step_report, 0, sizeof(step_report));
		uint8_t step_mod = 0;
		uint8_t slot = 2; // slots 0=modifier, 1=led, 2+ = keys
		for (int s = 0; s < MACRO_LEN && pos < USER_MACRO_LEN; s++, pos++)
		{
			uint16_t key = keys[pos];
			if (key == KC_NO)
			{
				pos++; // consume the separator
				break;
			}
			uint8_t mod = check_modifier(key);
			if (mod)
			{
				step_mod |= mod;
			}
			else if (slot < REPORT_LEN)
			{
				step_report[slot++] = (uint8_t)key;
			}
		}
		step_report[0] = step_mod;

		// send press
		if (BLE_EN == 1)
			xQueueSend(keyboard_q, (void *)step_report, (TickType_t)0);
		if (input_str_q != NULL)
			xQueueSend(input_str_q, (void *)step_report, (TickType_t)0);

		vTaskDelay(pdMS_TO_TICKS(MACRO_STEP_DELAY_MS));

		// send release
		if (BLE_EN == 1)
			xQueueSend(keyboard_q, (void *)release_report, (TickType_t)0);
		if (input_str_q != NULL)
			xQueueSend(input_str_q, (void *)release_report, (TickType_t)0);

		vTaskDelay(pdMS_TO_TICKS(MACRO_STEP_DELAY_MS));
	}
}

// used for debouncing
static uint32_t millis()
{
	return esp_timer_get_time() / 1000;
}

uint32_t prev_time = 0;
// adjust current layer
void layer_adjust(uint16_t keycode)
{
	uint32_t cur_time = millis();
	int layers_num = nvs_read_num_layers();
	if (cur_time - prev_time > DEBOUNCE)
	{
		if (layer_hold_flag == 0)
		{
			switch (keycode)
			{
			case DEFAULT:
				current_layout = 0;
				break;

			case LOWER:
				if (current_layout == 0)
				{
					for (int m = (layers_num - 1); m > 0; m--)
					{
						if (key_layouts[m - current_layout].active)
						{
							current_layout = m;
							break;
						}
					}
				}
				else
				{
					current_layout--;
				}

				break;

			case RAISE:

				if (current_layout == (layers_num - 1))
				{
					current_layout = 0;
					break;
				}
				if (current_layout < (layers_num - 1))
				{
					if (key_layouts[current_layout + 1].active)
					{
						current_layout++;
						break;
					}
					else
					{
						current_layout = 0;
						break;
					}
				}
				current_layout++;

				break;
			}
#ifdef OLED_ENABLE
			xQueueSend(layer_recieve_q, &current_layout, (TickType_t)0);
#endif

#ifdef RGB_LEDS
			rgb_mode_t led_mode;
			nvs_load_led_mode(&led_mode);
			xQueueSend(keyled_q, &led_mode, 0);

#endif
			ESP_LOGI(KEY_PRESS_TAG, "Layer modified!, Current layer: %d",
					 current_layout);
		}
	}
	prev_time = cur_time;
}

uint8_t matrix_prev_state[MATRIX_ROWS][MATRIX_COLS] = {0};

// checking the state of each key in the matrix
uint8_t *check_key_state(dd_layer *keymap)
{
	scan_matrix();
	for (uint8_t pad = 0; pad < KEYPADS; pad++)
	{

		uint8_t matrix_state[MATRIX_ROWS][MATRIX_COLS] = {0};
		memcpy(matrix_state, matrix_states[pad], sizeof(matrix_state));

		for (uint8_t col = (MATRIX_COLS * pad); col < ((pad + 1) * (MATRIX_COLS)); col++)
		{
			for (uint8_t row = 0; row < MATRIX_ROWS; row++)
			{

				// if there are no changes on this matrix position, skip to next position
				if (matrix_state[row][col] == matrix_prev_state[row][col])
					continue;

#ifdef RGB_LEDS
				rgb_key_led_press(row, col); // report the pressed key
#endif

				uint16_t report_index = (2 + col + row * KEYMAP_COLS);

				keycode = keymap->key_map[row][col];
				// ESP_LOGW("---", "keycode: %d", keycode);
				// //checking if the keycode is transparent
				// if (keycode == KC_TRNS) {
				// 	if (current_layout == 0) {
				// 		keycode = *default_layouts[MAX_LAYER][row][col];
				// 	} else {
				// 		keycode =
				// 				*default_layouts[current_layout - 1][row][col];
				// 	}
				// }

				led_status = check_led_status(keycode);
				if (matrix_state[row][col - MATRIX_COLS * pad] == 1)
				{

					// DISABLE PLUGIN_LAUNCHER. -> TODO ADJUST PLUGIN_BASE_VAL.
					//  // checking for function
					//  if (keycode >= PLUGIN_BASE_VAL)
					//  {
					//  	plugin_launcher(keycode);
					//  	continue;
					//  }

					// checking for layer hold
					if ((keycode >= LAYER_HOLD_BASE_VAL) && (keycode <= LAYER_HOLD_MAX_VAL))
					{
						if (layer_hold_flag == 0)
						{
							prev_layout = current_layout;
							current_layout = (keycode - LAYER_HOLD_BASE_VAL);
							layer_hold_flag = 1;
#ifdef OLED_ENABLE
							xQueueSend(layer_recieve_q, &current_layout,
									   (TickType_t)0);
#endif
							ESP_LOGI(KEY_PRESS_TAG,
									 "Layer modified!, Current layer: %d",
									 current_layout);
						}

						continue;
					}

					// checking for layer adjust keycodes
					// if ((keycode >= LAYERS_BASE_VAL) && (keycode < MACRO_BASE_VAL))
					if ((keycode > LAYER_ADJUST_MIN) && (keycode < LAYER_ADJUST_MAX))
					{
						// ESP_LOGW("---", "adjust");
						layer_adjust(keycode);
						continue;
					}

					// checking for macros
					if ((keycode >= MACRO_BASE_VAL) && (keycode <= MACRO_HOLD_MAX_VAL))
					{
						send_macro_sequence(keycode - MACRO_BASE_VAL);
						continue;
					}

					// checking for media control keycodes
					if ((keycode >= KC_MEDIA_NEXT_TRACK) && (keycode <= KC_AUDIO_VOL_DOWN))
					{
						media_control_send(keycode);
					}

					// checking for system control keycodes
					//				if((keycode>=0XA8)&&(keycode<=0XA7)){
					//					system_control(keycode);
					//					continue;
					//				}

					if (current_report[report_index] == 0)
					{
						modifier |= check_modifier(keycode);
						current_report[report_index] = keycode;
					}
				}
				if (matrix_state[row][col - MATRIX_COLS * pad] == 0)
				{

					// checking for layer hold release
					if ((key_layouts[prev_layout].key_map[row][col] >= LAYER_HOLD_BASE_VAL) && (keycode <= LAYER_HOLD_MAX_VAL) && (layer_hold_flag == 1))
					{
						current_layout = 0;
						layer_hold_flag = 0;
#ifdef OLED_ENABLE
						xQueueSend(layer_recieve_q, &current_layout,
								   (TickType_t)0);
#endif
						ESP_LOGI(KEY_PRESS_TAG,
								 "Layer modified!, Current layer: %d ",
								 current_layout);
					}

					// macro release is a no-op: sequence was fully sent on press
					if ((keycode >= MACRO_BASE_VAL) && (keycode <= MACRO_HOLD_MAX_VAL))
					{
						// nothing to clear — send_macro_sequence() sent its own release reports
					}

					if (current_report[report_index] != 0)
					{
						led_status = 0;
						

						modifier &= ~check_modifier(keycode);
						current_report[KEY_STATE[row][col]] = 0;
						current_report[report_index] = 0;

						// checking for media control keycodes
						if ((keycode >= KC_MEDIA_NEXT_TRACK) && (keycode <= KC_AUDIO_VOL_DOWN))
						{
							media_control_release(keycode);
						}
					}
				}
			}
		}
		memcpy(matrix_prev_state, matrix_state, sizeof(matrix_state));
	}
	current_report[0] = modifier;
	current_report[1] = led_status;
	return current_report;
}

#endif
