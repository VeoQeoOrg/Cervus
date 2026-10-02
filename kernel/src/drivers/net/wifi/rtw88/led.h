#ifndef __RTW_LED_H
#define __RTW_LED_H 
#ifdef CONFIG_RTW88_LEDS
void rtw_led_init(struct rtw_dev *rtwdev);
void rtw_led_deinit(struct rtw_dev *rtwdev);
#else
static inline void rtw_led_init(struct rtw_dev *rtwdev)
{
}
static inline void rtw_led_deinit(struct rtw_dev *rtwdev)
{
}
#endif
#endif
