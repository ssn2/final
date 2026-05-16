KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

MODULE_NAME := chr_drv

SRC_DIR := src

BUILD_DIR := $(PWD)/build
KO := $(BUILD_DIR)/$(MODULE_NAME).ko
BUILD_FILES += $(PWD)/*.ko
SOURCES := $(SRC_DIR)/*.c


CLANG_FORMAT_VER ?= 19
CLANG_FORMAT := clang-format-$(CLANG_FORMAT_VER)
CLANG_FORMAT_FLAGS += -i
FORMAT_FILES := $(SRC_DIR)/*.c

$(shell mkdir -p $(BUILD_DIR))

help:
	@echo "Цели:"
	@echo "  make kbuild      — собрать модуль ($(KO))"
	@echo "  make clean       — очистить сборку"
	@echo "  make format      — clang-format для $(SRC_DIR)/*.c"
	@echo "  make check       — checker/check.sh"
	@echo "  make load        — загрузить модуль (insmod сборка + загрузка, нужен root)"
	@echo "  make unload      — выгрузить модуль (rmmod)"
	@echo "  make install     — копия в /lib/modules/$(shell uname -r)/extra + depmod -a"
	@echo "  make uninstall   — удаление из extra + depmod -a"
	@echo "  make help        — этот список"

kbuild:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
	mv $(BUILD_FILES) $(BUILD_DIR)

format:
	$(CLANG_FORMAT) $(CLANG_FORMAT_FLAGS) $(FORMAT_FILES)

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rmdir $(BUILD_DIR)

check:
	checker/check.sh

# нужен root: sudo make load | unload | install | uninstall
load: kbuild
	insmod $(KO)

unload:
	-rmmod $(MODULE_NAME)

install: kbuild
	install -D -m 644 $(KO) /lib/modules/$(shell uname -r)/extra/$(MODULE_NAME).ko
	depmod -a

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/$(MODULE_NAME).ko
	depmod -a

.PHONY: help kbuild format clean check load unload install uninstall
