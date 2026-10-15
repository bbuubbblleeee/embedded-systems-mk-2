#ifndef INC_TIMER_APP_H_
#define INC_TIMER_APP_H_

/* Инициализация приложения-таймера: настраивает зуммер и рисует
 * начальный экран ввода. Вызывается один раз из main() после oled_Init(). */
void Timer_App_Init(void);

/* Один шаг главного цикла приложения. Не содержит блокирующих задержек,
 * поэтому должна вызываться максимально часто из while(1) в main(). */
void Timer_App_Run(void);

#endif /* INC_TIMER_APP_H_ */
