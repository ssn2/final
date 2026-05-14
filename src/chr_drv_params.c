/*
 * chr_drv_params.c — параметры модуля, видимые в sysfs
 * После загрузки модуля chr_drv.ko появляется каталог:
 *   /sys/module/chr_drv/parameters/
 * В нём файл debug И МОЖЕТ ЕЩЁ ЧЕГО ДОБВЛЮ.
 *
 * Зачем отдельный файл:
 *   Все настройки модуля в одном месте; основной код драйвера в chr_dev_main.c
 *
 * Пример загрузки с включённой отладкой:
 *   insmod chr_drv.ko debug=1
 * или из sysfs (если маска прав позволяет):
 *   echo 1 > /sys/module/chr_drv/parameters/debug
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h> 

#include "chr_drv_int.h" /* согласованное объявление extern bool debug */

/*
 * debug — булевый флаг «печатать ли лишние сообщения в журнал ядра».
 *
 * Зачем переменная:
 *   В chr_drv.c в open/release/write/init проверяется debug; если Y,
 *   в dmesg попадают дополнительные pr_info — удобно при отладке
 *
 * Определение (не extern) должно быть ровно в одном .c файле модуля — здесь.
 */
bool debug;

/*
 * module_param_named → /sys/module/chr_drv/parameters/debug
 * (не /sys/class/... — класс устройства и его атрибуты в chr_drv_main.c).
 *
 * То же значение debug можно менять через ioctl CHR_DRV_IOC_SET_DEBUG.
 */
module_param_named(debug, debug, bool, 0644);
MODULE_PARM_DESC(debug,
		 "Если 1 — больше отладочных сообщений в журнал ядра (printk)");
