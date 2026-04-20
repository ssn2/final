/*
 * chr_drv_main.c
 * Собственно сам драйвер. Пока всё в одном файле,
 * т.к. простенько всё...
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>

#include "chr_drv.h"
#include "chr_drv_int.h"

int chr_drv_init(void)
{
	return 0;
}

void chr_drv_exit(void)
{
}
