/*
 * chr_drv.c — только «обёртка» модуля: module_init, module_exit, MODULE_*
 * точка входа в insmod/rmmod, вся работа с устройством будет в другом файле  !!!! ПЕРЕДЕЛАТЬ ПОТОМ !!!
 *
 * Параметры модуля (debug) в chr_drv_params.c
 * Это я придумал чтобы потренироваться в инициализациях модулей, надо чо-нить более полезное
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>

#include "chr_drv.h"
#include "chr_drv_int.h"

/*
 * chr_drv_mod_init — функция, которую ядро вызывает сразу после insmod
 *
 * Входные параметры: нет
 * Возвращаемое значение:
 *   0 — модуль остаётся загруженным;
 *   отрицательный код — загрузка отменяется, ядро выгрузит модуль и вернёт ошибку insmod.
 * Надо бы подумать про modprobe
 */
static int __init chr_drv_mod_init(void)
{
	int ret = chr_drv_init();

	if (ret)
		pr_err("%s v%s: init failed: %d\n", CHR_DRV_DEVICE_NAME,
		       CHR_DRV_VERSION, ret);
	return ret;
}

/*
 * chr_drv_mod_exit — вызывается ядром перед выгрузкой модуля (rmmod).
 *
 * Входные параметры: нет.
 * Возвращаемое значение: нет (void).
 * Освободить всё, что chr_drv_init зарегистрировал,
 */
static void __exit chr_drv_mod_exit(void)
{
	chr_drv_exit();
}


module_init(chr_drv_mod_init);
module_exit(chr_drv_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Smolovoy Sergey");
MODULE_DESCRIPTION("Fusy char driver");
MODULE_VERSION(CHR_DRV_VERSION);
