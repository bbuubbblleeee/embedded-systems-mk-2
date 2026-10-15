#include "timer_app.h"
#include "main.h"
#include "kb_scan.h"
#include "oled.h"
#include "fonts.h"
#include "buzzer.h"
#include "sdk_uart.h"
#include <stdio.h>
#include <stdlib.h>

/* Период опроса клавиатуры. Каждый опрос - это до 4х обращений по I2C
 * (по одному на строку матрицы), поэтому опрашивать чаще, чем нажимает
 * человек, не требуется - это только лишняя нагрузка на шину I2C. */
#define KB_SCAN_PERIOD_MS     20

#define INPUT_MAX_DIGITS      4      /* максимум 9999 секунд (166:39) */

#define ALARM_TONE_HZ         2000   /* частота звукового сигнала, Гц */
#define ALARM_BEEP_PERIOD_MS  300    /* период мигания/пищания сигнала */
#define ALARM_AUTO_STOP_MS    10000  /* автоостановка сигнала, если никто не подошел */

typedef enum {
	APP_INPUT = 0,   /* ввод начального значения таймера с клавиатуры */
	APP_RUNNING,     /* обратный отсчет идет */
	APP_PAUSED,      /* обратный отсчет приостановлен */
	APP_ALARM        /* время истекло, подается звуковой сигнал */
} app_state_t;

static app_state_t state;

static char     inputBuf[INPUT_MAX_DIGITS + 1];
static uint8_t  inputLen;

static uint32_t remainingSec;
static uint32_t lastSecTick;

static uint32_t alarmStartTick;
static uint32_t alarmToggleTick;
static uint8_t  alarmToneOn;

/* ---------------------------- Экран: ввод значения ---------------------------- */
static void Draw_Input_Screen(void)
{
	char buf[8];

	oled_Fill(Black);

	oled_SetCursor(0, 0);
	oled_WriteString("Timer: enter sec", Font_7x10, White);
	oled_SetCursor(0, 14);
	oled_WriteString("#=start  *=clear", Font_7x10, White);

	snprintf(buf, sizeof(buf), "%-4s", inputLen ? inputBuf : "0");
	oled_SetCursor(20, 34);
	oled_WriteString(buf, Font_16x26, White);

	oled_UpdateScreen();
}

/* ------------------------- Экран: идет/приостановлен отсчет ------------------- */
static void Draw_Time_Screen(const char *title)
{
	char buf[8];
	unsigned long mm = remainingSec / 60;
	unsigned long ss = remainingSec % 60;

	oled_Fill(Black);

	oled_SetCursor(0, 0);
	oled_WriteString((char *)title, Font_7x10, White);

	snprintf(buf, sizeof(buf), "%02lu:%02lu", mm, ss);
	oled_SetCursor(16, 30);
	oled_WriteString(buf, Font_16x26, White);

	oled_UpdateScreen();
}

/* ------------------------------ Экран: сигнал тревоги -------------------------- */
static void Draw_Alarm_Screen(uint8_t blinkOn)
{
	oled_Fill(Black);

	if (blinkOn) {
		oled_SetCursor(0, 0);
		oled_WriteString("TIME'S UP!", Font_11x18, White);
	}

	oled_SetCursor(16, 30);
	oled_WriteString("00:00", Font_16x26, White);

	oled_SetCursor(0, 52);
	oled_WriteString("Any key = stop", Font_7x10, White);

	oled_UpdateScreen();
}

/* ---------------------------------- Переходы состояний -------------------------- */
static void Enter_Input_State(void)
{
	state = APP_INPUT;
	inputLen = 0;
	inputBuf[0] = 0;
	Buzzer_Off();
	Draw_Input_Screen();
}

static void Start_Countdown(void)
{
	uint32_t sec = inputLen ? (uint32_t)atoi(inputBuf) : 0;
	if (sec == 0) {
		return; /* пустой/нулевой ввод игнорируем */
	}

	remainingSec = sec;
	lastSecTick = HAL_GetTick();
	state = APP_RUNNING;
	Draw_Time_Screen("Running  #=pause");
	UART_Transmit((uint8_t *)"Timer started\r\n");
}

static void Enter_Alarm_State(void)
{
	state = APP_ALARM;
	alarmStartTick = HAL_GetTick();
	alarmToggleTick = alarmStartTick;
	alarmToneOn = 1;
	Buzzer_On(ALARM_TONE_HZ);
	Draw_Alarm_Screen(1);
	UART_Transmit((uint8_t *)"Time is up!\r\n");
}

/* -------------------------------- Обработчики клавиш ---------------------------- */
static void Handle_Input_Key(char key)
{
	if (key >= '0' && key <= '9') {
		if (inputLen < INPUT_MAX_DIGITS) {
			inputBuf[inputLen++] = key;
			inputBuf[inputLen] = 0;
			Draw_Input_Screen();
		}
	} else if (key == '*') {
		inputLen = 0;
		inputBuf[0] = 0;
		Draw_Input_Screen();
	} else if (key == '#') {
		Start_Countdown();
	}
}

static void Handle_Running_Key(char key)
{
	if (key == '#') {
		state = APP_PAUSED;
		Draw_Time_Screen("Paused   #=go");
	} else if (key == '*') {
		Enter_Input_State();
	}
}

static void Handle_Paused_Key(char key)
{
	if (key == '#') {
		lastSecTick = HAL_GetTick();
		state = APP_RUNNING;
		Draw_Time_Screen("Running  #=pause");
	} else if (key == '*') {
		Enter_Input_State();
	}
}

/* ------------------------------------- API -------------------------------------- */
void Timer_App_Init(void)
{
	Buzzer_Init();
	Enter_Input_State();
}

void Timer_App_Run(void)
{
	static uint32_t lastScanTick = 0;

	uint32_t now = HAL_GetTick();
	char key = 0;

	if ((uint32_t)(now - lastScanTick) >= KB_SCAN_PERIOD_MS) {
		lastScanTick = now;
		key = KB_GetKeyEvent();
	}

	switch (state) {
	case APP_INPUT:
		if (key) {
			Handle_Input_Key(key);
		}
		break;

	case APP_RUNNING:
		if (key) {
			Handle_Running_Key(key);
		}
		if (state == APP_RUNNING && (uint32_t)(now - lastSecTick) >= 1000) {
			lastSecTick += 1000;
			if (remainingSec > 0) {
				remainingSec--;
			}
			if (remainingSec == 0) {
				Enter_Alarm_State();
			} else {
				Draw_Time_Screen("Running  #=pause");
			}
		}
		break;

	case APP_PAUSED:
		if (key) {
			Handle_Paused_Key(key);
		}
		break;

	case APP_ALARM:
		if (key) {
			Enter_Input_State();
			break;
		}
		if ((uint32_t)(now - alarmStartTick) >= ALARM_AUTO_STOP_MS) {
			Enter_Input_State();
			break;
		}
		if ((uint32_t)(now - alarmToggleTick) >= ALARM_BEEP_PERIOD_MS) {
			alarmToggleTick = now;
			alarmToneOn = !alarmToneOn;
			if (alarmToneOn) {
				Buzzer_On(ALARM_TONE_HZ);
			} else {
				Buzzer_Off();
			}
			Draw_Alarm_Screen(alarmToneOn);
		}
		break;
	}
}
