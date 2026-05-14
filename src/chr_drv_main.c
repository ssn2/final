/*
 * chr_drv_main.c — символьный драйвер: регистрация и I/O.
 *
 * Интерфейсы после insmod:
 *   /dev/chr_drv                         — read, write, ioctl
 *   /proc/chr_drv                        — статус и содержимое буфера
 *   /sys/class/chrdrvclass/chr_drv/      — length, buffer (sysfs устройства)
 *
 *  атрибуты устройса в sysfs    /sys/class/chrdrvclass/chr_drv/...
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#include "chr_drv.h"
#include "chr_drv_int.h"

/* Регистрация в ядре */
static dev_t chr_dev;
static struct cdev chr_cdev;
static struct class *chr_class;
static struct device *chr_device;
static struct proc_dir_entry *chr_proc;

/*
 * Буфер устройства — данные между write и read.
 * kbuffer_len — сколько байт реально записано (не весь массив).
 * chr_buf_mutex — защита от гонок при параллельных read/write/ioctl.
 */
static char kbuffer[CHR_DRV_BUFFER_SIZE];
static size_t kbuffer_len;
static DEFINE_MUTEX(chr_buf_mutex);

/* -------------------------------------------------------------------------- */
/* file_operations                                                            */
/* -------------------------------------------------------------------------- */

/*
 * open — вызывается при open("/dev/chr_drv", ...).
 * Для простого драйвера достаточно вернуть 0; отдельную память на файл не выделяем.
 */
static int chr_drv_open(struct inode *inode, struct file *file)
{
	if (debug)
		pr_info("%s v%s: open (minor %u)\n", CHR_DRV_DEVICE_NAME,
			CHR_DRV_VERSION, iminor(inode));
	return 0;
}

static int chr_drv_release(struct inode *inode, struct file *file)
{
	if (debug)
		pr_info("%s v%s: release\n", CHR_DRV_DEVICE_NAME,
			CHR_DRV_VERSION);
	return 0;
}

/*
 * read — копируем из kbuffer в буфер пользователя (copy_to_user).
 *
 * ppos — смещение в «файле»; 0 после open, растёт после каждого read.
 * Если ppos >= kbuffer_len, данных больше нет → возвращаем 0 (EOF).
 */
static ssize_t chr_drv_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	ssize_t ret;

	if (!buf)
		return -EINVAL;

	if (*ppos >= kbuffer_len)
		return 0;

	count = min(count, kbuffer_len - (size_t)*ppos);

	mutex_lock(&chr_buf_mutex);
	if (copy_to_user(buf, kbuffer + *ppos, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	*ppos += count;
	ret = count;

out_unlock:
	mutex_unlock(&chr_buf_mutex);
	return ret;
}

/*
 * write — копируем из userspace в kbuffer (copy_from_user).
 *
 * Запись с учётом ppos; kbuffer_len = max(старое, ppos + записано).
 * За пределы CHR_DRV_BUFFER_SIZE писать нельзя → -ENOSPC.
 */
static ssize_t chr_drv_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	size_t to_copy;

	if (!buf)
		return -EINVAL;

	if (*ppos >= CHR_DRV_BUFFER_SIZE)
		return -ENOSPC;

	to_copy = min(count, (size_t)CHR_DRV_BUFFER_SIZE - (size_t)*ppos);

	mutex_lock(&chr_buf_mutex);
	if (copy_from_user(kbuffer + *ppos, buf, to_copy)) {
		mutex_unlock(&chr_buf_mutex);
		return -EFAULT;
	}
	*ppos += to_copy;
	if ((size_t)*ppos > kbuffer_len)
		kbuffer_len = *ppos;
	mutex_unlock(&chr_buf_mutex);

	if (debug)
		pr_info("%s v%s: write %zu bytes, len=%zu\n", CHR_DRV_DEVICE_NAME,
			CHR_DRV_VERSION, to_copy, kbuffer_len);

	return to_copy;
}

/*
 * ioctl — управление драйвером из userspace.
 *
 * -ENOTTY — неизвестная команда; -EFAULT — ошибка copy_to/from_user;
 * -EINVAL — неверный аргумент (NULL).
 */
static long chr_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	size_t len;
	int dbg;

	if (_IOC_TYPE(cmd) != CHR_DRV_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case CHR_DRV_IOC_CLEAR:
		mutex_lock(&chr_buf_mutex);
		kbuffer_len = 0;
		memset(kbuffer, 0, sizeof(kbuffer));
		mutex_unlock(&chr_buf_mutex);
		if (debug)
			pr_info("%s v%s: ioctl CLEAR\n", CHR_DRV_DEVICE_NAME,
				CHR_DRV_VERSION);
		return 0;

	case CHR_DRV_IOC_GET_LEN:
		if (!arg)
			return -EINVAL;
		mutex_lock(&chr_buf_mutex);
		len = kbuffer_len;
		mutex_unlock(&chr_buf_mutex);
		if (copy_to_user((void __user *)arg, &len, sizeof(len)))
			return -EFAULT;
		return 0;

	case CHR_DRV_IOC_GET_DEBUG:
		if (!arg)
			return -EINVAL;
		dbg = debug ? 1 : 0;
		if (copy_to_user((void __user *)arg, &dbg, sizeof(dbg)))
			return -EFAULT;
		return 0;

	case CHR_DRV_IOC_SET_DEBUG:
		if (get_user(dbg, (int __user *)arg))
			return -EFAULT;
		debug = !!dbg;
		if (debug)
			pr_info("%s v%s: ioctl SET_DEBUG=1\n",
				CHR_DRV_DEVICE_NAME, CHR_DRV_VERSION);
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations chr_drv_fops = {
	.owner = THIS_MODULE,
	.open = chr_drv_open,
	.release = chr_drv_release,
	.read = chr_drv_read,
	.write = chr_drv_write,
	.unlocked_ioctl = chr_drv_ioctl,
	.llseek = default_llseek,
};

/* -------------------------------------------------------------------------- */
/* sysfs: /sys/class/chrdrvclass/chr_drv/length и .../buffer                  */
/* -------------------------------------------------------------------------- */

/*
 * length — сколько байт сейчас в буфере (чтение: cat .../length).
 */
static ssize_t length_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t n;

	mutex_lock(&chr_buf_mutex);
	n = sysfs_emit(buf, "%zu\n", kbuffer_len);
	mutex_unlock(&chr_buf_mutex);
	return n;
}

/*
 * buffer — содержимое буфера в виде строки (непечатаемые символы — как есть).
 * Для бинарных данных удобнее смотреть hexdump в userspace; здесь — учебный вывод.
 */
static ssize_t buffer_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t n;

	mutex_lock(&chr_buf_mutex);
	n = sysfs_emit(buf, "%.*s\n", (int)kbuffer_len, kbuffer);
	mutex_unlock(&chr_buf_mutex);
	return n;
}

static DEVICE_ATTR_RO(length);
static DEVICE_ATTR_RO(buffer);

static int chr_drv_sysfs_create(struct device *dev)
{
	int ret;

	ret = device_create_file(dev, &dev_attr_length);
	if (ret)
		return ret;

	ret = device_create_file(dev, &dev_attr_buffer);
	if (ret) {
		device_remove_file(dev, &dev_attr_length);
		return ret;
	}
	return 0;
}

static void chr_drv_sysfs_remove(struct device *dev)
{
	if (!dev)
		return;
	device_remove_file(dev, &dev_attr_buffer);
	device_remove_file(dev, &dev_attr_length);
}

/* -------------------------------------------------------------------------- */
/* /proc/chr_drv                                                              */
/* -------------------------------------------------------------------------- */

static int chr_drv_proc_show(struct seq_file *m, void *v)
{
	mutex_lock(&chr_buf_mutex);
	seq_printf(m, "device\t%s\n", CHR_DRV_DEVICE_NAME);
	seq_printf(m, "version\t%s\n", CHR_DRV_VERSION);
	seq_printf(m, "major\t%u\n", MAJOR(chr_dev));
	seq_printf(m, "minor\t%u\n", MINOR(chr_dev));
	seq_printf(m, "buffer_size\t%d\n", CHR_DRV_BUFFER_SIZE);
	seq_printf(m, "data_len\t%zu\n", kbuffer_len);
	seq_printf(m, "debug\t%d\n", debug ? 1 : 0);
	seq_printf(m, "buffer\t%.*s\n", (int)kbuffer_len, kbuffer);
	mutex_unlock(&chr_buf_mutex);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Откат регистрации                                                          */
/* -------------------------------------------------------------------------- */
/* Откат шага 1 (alloc_chrdev_region) */
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

/* Откат шага 4 (device_create + sysfs; /dev/chr_drv) */
static void chr_drv_drop_device(void)
{
	if (chr_device) {
		chr_drv_sysfs_remove(chr_device);
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

	/* Шаг 4: device_create - /dev/chr_drv; при сбое — откат 3, 2, 1 */
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

	/* sysfs-атрибуты на устройстве (шаг 5; при сбое — откат 4, 3, 2, 1) */
	ret = chr_drv_sysfs_create(chr_device);
	if (ret) {
		pr_err("%s v%s: sysfs create failed: %d\n", CHR_DRV_DEVICE_NAME,
		       CHR_DRV_VERSION, ret);
		chr_drv_drop_device();
		chr_drv_drop_class();
		chr_drv_drop_cdev();
		chr_drv_drop_region();
		return ret;
	}

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
