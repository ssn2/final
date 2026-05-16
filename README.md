# chr_drv — символьный драйвер ядра Linux

Учебный модуль ядра: символьное устройство с операциями `read`, `write`, `ioctl`, регистрацией в `/dev`, `/proc` и `/sys`. Данные хранятся во внутреннем буфере размером 256 байт.

**Версия:** 1.0  
**Автор:** Smolovoy Sergey  
**Лицензия:** GPL

## Структура проекта

```
Final_linux_kernel/
├── Makefile              # сборка, load/unload, install
├── Kbuild                # список объектов модуля
├── src/
│   ├── chr_drv.c         # module_init / module_exit
│   ├── chr_drv_main.c    # file_operations, регистрация, /proc, sysfs устройства
│   ├── chr_drv_params.c  # параметр модуля debug
│   ├── chr_drv.h         # chr_drv_init / chr_drv_exit
│   └── chr_drv_int.h     # константы, ioctl, размер буфера
└── build/
    └── chr_drv.ko        # собранный модуль (после make kbuild)
```

## Требования

- Заголовки ядра для текущей версии: `linux-headers-$(uname -r)`
- `make`, компилятор GCC (как у сборки ядра)
- Для `make load`, `install` — права root (`sudo`)

Если при `insmod` появляется `Unknown symbol __x86_return_thunk`, это расхождение версий GCC (ядро собрано другим пакетом `gcc`, чем в системе). В `Kbuild` уже добавлен флаг `-mfunction-return=keep` для совместимости.

## Сборка и загрузка

```bash
make kbuild          # собрать build/chr_drv.ko
make load            # собрать и загрузить (insmod)
make unload          # выгрузить (rmmod)
make clean           # очистить артефакты сборки
make help            # список целей
```

Загрузка с включённой отладкой в журнале ядра:

```bash
sudo insmod build/chr_drv.ko debug=1
# или после make load:
echo 1 | sudo tee /sys/module/chr_drv/parameters/debug
```

Проверка:

```bash
lsmod | grep chr_drv
modinfo build/chr_drv.ko
dmesg | tail
ls -l /dev/chr_drv /proc/chr_drv
ls -l /sys/class/chrdrvclass/chr_drv/
```

## Установка в систему

Копирует модуль в `/lib/modules/$(uname -r)/extra/` и обновляет зависимости:

```bash
sudo make install
sudo modprobe chr_drv          # загрузка (опционально)
sudo make uninstall            # удаление из extra + depmod -a
```

## Интерфейсы после загрузки

| Путь | Назначение |
|------|------------|
| `/dev/chr_drv` | Основное устройство: read, write, ioctl |
| `/proc/chr_drv` | Статус: версия, major/minor, размер буфера, длина данных, флаг debug, содержимое буфера |
| `/sys/class/chrdrvclass/chr_drv/length` | Текущая длина данных в буфере (read-only) |
| `/sys/class/chrdrvclass/chr_drv/buffer` | Содержимое буфера как строка (read-only) |
| `/sys/module/chr_drv/parameters/debug` | Параметр модуля: подробный printk (0/1) |

Класс устройства в sysfs: `chrdrvclass`, имя узла: `chr_drv`.

## Примеры: read и write

```bash
# запись в буфер драйвера
echo -n 'Hello, kernel!' | sudo tee /dev/chr_drv

# чтение (с начала, пока не исчерпан буфер)
sudo cat /dev/chr_drv

# повторное чтение без новой записи — пусто (EOF)
sudo cat /dev/chr_drv

# просмотр через proc и sysfs
cat /proc/chr_drv
cat /sys/class/chrdrvclass/chr_drv/length
cat /sys/class/chrdrvclass/chr_drv/buffer
```

Запись за пределы 256 байт (с учётом смещения) вернёт ошибку `ENOSPC`.

## ioctl

Команды (magic `'c'`, см. `src/chr_drv_int.h`):

| Команда | Направление | Действие |
|---------|-------------|----------|
| `CHR_DRV_IOC_CLEAR` | — | Очистить буфер |
| `CHR_DRV_IOC_GET_LEN` | read | Вернуть `size_t` — текущую длину данных |
| `CHR_DRV_IOC_GET_DEBUG` | read | Вернуть `int`: 0/1 — флаг debug |
| `CHR_DRV_IOC_SET_DEBUG` | write | Установить debug (0 или 1) |

Минимальная программа для userspace (`test_ioctl.c`):

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHR_DRV_IOC_MAGIC 'c'
#define CHR_DRV_IOC_CLEAR     _IO(CHR_DRV_IOC_MAGIC, 0)
#define CHR_DRV_IOC_GET_LEN   _IOR(CHR_DRV_IOC_MAGIC, 1, size_t)
#define CHR_DRV_IOC_GET_DEBUG _IOR(CHR_DRV_IOC_MAGIC, 2, int)
#define CHR_DRV_IOC_SET_DEBUG _IOW(CHR_DRV_IOC_MAGIC, 3, int)

int main(void)
{
	int fd = open("/dev/chr_drv", O_RDWR);
	size_t len;
	int dbg;

	if (fd < 0) {
		perror("open");
		return 1;
	}

	write(fd, "ioctl test", 10);

	if (ioctl(fd, CHR_DRV_IOC_GET_LEN, &len) == 0)
		printf("len = %zu\n", len);

	ioctl(fd, CHR_DRV_IOC_SET_DEBUG, &(int){1});
	ioctl(fd, CHR_DRV_IOC_GET_DEBUG, &dbg);
	printf("debug = %d\n", dbg);

	ioctl(fd, CHR_DRV_IOC_CLEAR, NULL);
	ioctl(fd, CHR_DRV_IOC_GET_LEN, &len);
	printf("len after clear = %zu\n", len);

	close(fd);
	return 0;
}
```

Сборка и запуск:

```bash
gcc -Wall -o test_ioctl test_ioctl.c
sudo ./test_ioctl
```

## Обработка ошибок

- Проверка указателей userspace (`NULL` → `-EINVAL`, ошибка копирования → `-EFAULT`)
- Неизвестная команда ioctl → `-ENOTTY`
- При сбое инициализации выполняется откат уже зарегистрированных ресурсов (region, cdev, class, device, proc)
- `chr_drv_exit` снимает регистрации в обратном порядке

## Дополнительные цели Makefile

```bash
make format    # clang-format для src/*.c (нужен clang-format-19)
make check     # checker/check.sh (если скрипт проверки добавлен в репозиторий)
```

## Тестирование

1. **read/write** — `echo` / `cat` на `/dev/chr_drv`, сверка с `/proc/chr_drv` и sysfs-атрибутами.
2. **ioctl** — программа выше: длина буфера, clear, переключение debug.
3. **proc/sys** — после записи данные видны в `/proc/chr_drv`, `length` и `buffer` в sysfs.
4. **параметр debug** — `insmod ... debug=1` или ioctl `SET_DEBUG`, в `dmesg` появляются дополнительные `pr_info` при open/write/ioctl.

Выгрузка:

```bash
sudo make unload
# или
sudo rmmod chr_drv
```
