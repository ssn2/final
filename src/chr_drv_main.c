/*
 * chr_drv_main.c — сам драйвер
 *
 * Сейчас только «появление» драйвера в системе:
 *   /dev/chr_drv              — узел для программ (open/read/write позже)
 *   /sys/class/chrdrvclass/   — класс в sysfs (udev)
 *   /proc/chr_drv             — краткий отчёт о регистрации
 *
 * read/write/ioctl — пока заглушки, допишу позже.
 *
 * Вызов: chr_drv.c (module_init - chr_drv_init).
 * Константы: chr_drv_int.h; параметр debug: chr_drv_params.c.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* cdev.h, device.h — регистрация; fs.h — file_operations; proc — /proc/chr_drv */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "chr_drv.h"
#include "chr_drv_int.h"

/*
 * Состояние драйвера (static — видно только в этом файле), живёт пока модуль загружен.
 * chr_dev — major:minor после alloc_chrdev_region;
 * chr_cdev — связка номера с chr_drv_fops;
 * chr_class / chr_device — sysfs и /dev/chr_drv;
 * chr_proc — дескриптор /proc/chr_drv.
 */
static dev_t chr_dev;
static struct cdev chr_cdev;
static struct class *chr_class;
static struct device *chr_device;
static struct proc_dir_entry *chr_proc;

/* Заглушки file_operations — cdev_add требует таблицу, даже без реального I/O */

/* open: разрешаем открытие /dev/chr_drv; при debug — сообщение в dmesg */
static int chr_drv_open(struct inode *inode, struct file *file)
{
	if (debug)
		pr_info("%s v%s: open stub (minor %u)\n", CHR_DRV_DEVICE_NAME,
			CHR_DRV_VERSION, iminor(inode));
	return 0;
}

/* release: close файла; не путать с chr_drv_exit (rmmod всего модуля) */
static int chr_drv_release(struct inode *inode, struct file *file)
{
	if (debug)
		pr_info("%s v%s: release stub\n", CHR_DRV_DEVICE_NAME,
			CHR_DRV_VERSION);
	return 0;
}

/* read: заглушка (-ENOSYS); позже — copy_to_user */
static ssize_t chr_drv_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	return -ENOSYS;
}

/* write: заглушка (-ENOSYS); позже — copy_from_user */
static ssize_t chr_drv_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	return -ENOSYS;
}

/*
 * ioctl — заглушка. -ENOTTY = «нет такой команды для этого устройства».
 */
static long chr_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}

/* Таблица операций для ядра; .owner — удержание модуля, пока файл открыт */
static const struct file_operations chr_drv_fops = {
	.owner = THIS_MODULE,
	.open = chr_drv_open,
	.release = chr_drv_release,
	.read = chr_drv_read,
	.write = chr_drv_write,
	.unlocked_ioctl = chr_drv_ioctl,
};

/* /proc/chr_drv — статус регистрации (cat /proc/chr_drv) */

static int chr_drv_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "device\t%s\n", CHR_DRV_DEVICE_NAME);
	seq_printf(m, "version\t%s\n", CHR_DRV_VERSION);
	seq_printf(m, "major\t%u\n", MAJOR(chr_dev));
	seq_printf(m, "minor\t%u\n", MINOR(chr_dev));
	seq_printf(m, "status\tregistered (I/O stubs)\n");
	return 0;
}

/*
 * Откат регистрации — каждая drop_* снимает свой шаг init (номера совпадают).
 * При ошибке в chr_drv_init вызываются только drop для уже выполненных шагов.
 * chr_drv_unregister — полный откат в обратном порядке: 5 -4 - 3 - 2 - 1.
 */

/* Откат шага 1 инициализации (alloc_chrdev_region) */
static void chr_drv_drop_region(void)
{
	unregister_chrdev_region(chr_dev, 1);
}

/* Откат шага 2 (cdev_add) */
static void chr_drv_drop_cdev(void)
{
	cdev_del(&chr_cdev);
}

/* Откат шага 3 (class_create) */
static void chr_drv_drop_class(void)
{
	if (chr_class) {
		class_destroy(chr_class);
		chr_class = NULL;
	}
}

/* Откат шага 4 (device_create, /dev/chr_drv) */
static void chr_drv_drop_device(void)
{
	if (chr_device) {
		device_destroy(chr_class, chr_dev);
		chr_device = NULL;
	}
}

/* Откат шага 5 (proc_create_single, /proc/chr_drv) */
static void chr_drv_drop_proc(void)
{
	if (chr_proc) {
		proc_remove(chr_proc);
		chr_proc = NULL;
	}
}

/* Полный откат шагов 5→1: rmmod или сбой на шаге 5 init */
static void chr_drv_unregister(void)
{
	chr_drv_drop_proc();    /* шаг 5 */
	chr_drv_drop_device();  /* шаг 4 */
	chr_drv_drop_class();   /* шаг 3 */
	chr_drv_drop_cdev();    /* шаг 2 */
	chr_drv_drop_region();  /* шаг 1 */
}

/*
 * chr_drv_init — регистрация драйвера (insmod).
 * Шаги 1…5 выполняются по порядку; при ошибке — откат только пройденных шагов.
 * Не знаю, это просто инициализация, поэтому потерь времени быть не должно
 * из-за кучи вызовов функций, тем более это при ошибке только
 * Зато я вроде уже не запутался что за чем и куда и так проще 
 * мне откатывать при ошибке
 */
int chr_drv_init(void)
{
	int ret;

	/* Шаг 1: запрашиваем у ядра major/minor, результат — в chr_dev */
	ret = alloc_chrdev_region(&chr_dev, 0, 1, CHR_DRV_DEVICE_NAME);
	if (ret) {
		pr_err("%s v%s: alloc_chrdev_region failed: %d\n",
		       CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION, ret);
		return ret;
	}

	/* Шаг 2: cdev_init + cdev_add; при сбое — откат шага 1 (cdev_del не нужен) */
	cdev_init(&chr_cdev, &chr_drv_fops);
	ret = cdev_add(&chr_cdev, chr_dev, 1);
	if (ret) {
		pr_err("%s v%s: cdev_add failed: %d\n", CHR_DRV_DEVICE_NAME,
		       CHR_DRV_VERSION, ret);
		chr_drv_drop_region();
		return ret;
	}

	/* Шаг 3: класс в sysfs; при сбое — откат шагов 2 и 1 */
	chr_class = class_create(THIS_MODULE, CHR_DRV_CLASS_NAME);
	if (IS_ERR(chr_class)) {
		ret = PTR_ERR(chr_class);
		chr_class = NULL;
		pr_err("%s v%s: class_create failed: %d\n",
		       CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION, ret);
		chr_drv_drop_cdev();
		chr_drv_drop_region();
		return ret;
	}

	/* Шаг 4: device_create → /dev/chr_drv; при сбое — откат 3, 2, 1 */
	chr_device = device_create(chr_class, NULL, chr_dev, NULL,
				   CHR_DRV_DEVICE_NAME);
	if (IS_ERR(chr_device)) {
		ret = PTR_ERR(chr_device);
		chr_device = NULL;
		pr_err("%s v%s: device_create failed: %d\n",
		       CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION, ret);
		chr_drv_drop_class();
		chr_drv_drop_cdev();
		chr_drv_drop_region();
		return ret;
	}

	/* Шаг 5: /proc/chr_drv; при сбое — полный откат 5→1 (proc ещё не создан) */
	chr_proc = proc_create_single(CHR_DRV_PROC_NAME, 0444, NULL,
				      chr_drv_proc_show);
	if (!chr_proc) {
		ret = -ENOMEM;
		pr_err("%s v%s: proc_create_single failed\n",
		       CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION);
		chr_drv_unregister();
		return ret;
	}

    pr_info("%s v%s: loaded", CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION);
    if (debug)
	    pr_info("  debug enabled\n");
	
	pr_info("%s v%s: registered (major %u, minor %u)\n",
		CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION, MAJOR(chr_dev),
		MINOR(chr_dev));
	return 0;
}

/* chr_drv_exit — rmmod, полный откат шагов 5→1 */
void chr_drv_exit(void)
{
	chr_drv_unregister();
	pr_info("%s v%s: unregistered\n", CHR_DRV_DEVICE_NAME,
		CHR_DRV_VERSION);
	pr_info("%s v%s: unloaded\n", CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION);
}
