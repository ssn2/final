/*
 * chr_drv_int.h — общие константы и объявления 
 * только для кода ядра этого модуля.
 * Что может в userspace вызываться будет отдельно
 */
#ifndef CHR_DRV_INT_H
#define CHR_DRV_INT_H

#include <linux/types.h>

/*
 * debug — флаг подробного логирования.
 * Определение переменной: params.c 
 * Использование: chr_drv_main.c (проверки if (debug) pr_info(...)).
 */
extern bool debug;

/*
 * CHR_DRV_VERSION — версия модуля (modinfo, сообщения в dmesg).
 */
#define CHR_DRV_VERSION "1.0"

/*
 * CHR_DRV_DEVICE_NAME — строка имени узла в /dev (без пути /dev/).
 * Передаётся в device_create(..., name).
 */
#define CHR_DRV_DEVICE_NAME "chr_drv"

/*
 * CHR_DRV_CLASS_NAME — имя класса в sysfs: появится /sys/class/finalclass.
 */
#define CHR_DRV_CLASS_NAME "chrdrvclass"

/*
 * CHR_DRV_PROC_NAME — имя файла только в каталоге /proc (без пути).
 */
#define CHR_DRV_PROC_NAME "chr_drv"

/*
 * CHR_DRV_BUFFER_SIZE — размер статического массива kbuffer в chr_drv_main.c
 * в байтах. Ограничивает максимум данных, которые можно держать в буфере.
 */
#define CHR_DRV_BUFFER_SIZE 256

/*
 * ioctl — команды для userspace (ioctl(2) на /dev/chr_drv).
 * Magic 'c' — произвольная буква, главное не пересечься с другими драйверами.
 *
 * Примеры из shell (нужен свой маленький C-файл или программа):
 *   CHR_DRV_IOC_CLEAR     — очистить буфер
 *   CHR_DRV_IOC_GET_LEN   — узнать, сколько байт сейчас в буфере
 *   CHR_DRV_IOC_GET_DEBUG / SET_DEBUG — читать/менять флаг debug модуля
 */
#define CHR_DRV_IOC_MAGIC	'c'

#define CHR_DRV_IOC_CLEAR	_IO(CHR_DRV_IOC_MAGIC, 0)
#define CHR_DRV_IOC_GET_LEN	_IOR(CHR_DRV_IOC_MAGIC, 1, size_t)
#define CHR_DRV_IOC_GET_DEBUG	_IOR(CHR_DRV_IOC_MAGIC, 2, int)
#define CHR_DRV_IOC_SET_DEBUG	_IOW(CHR_DRV_IOC_MAGIC, 3, int)

#endif /* CHR_DRV_INT_H */
