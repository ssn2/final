#!/usr/bin/env bash
# Автотесты модуля chr_drv: сборка, load, /dev|/proc|/sys, read/write, ioctl.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODULE=chr_drv
KO="$ROOT/build/${MODULE}.ko"
DEV="/dev/${MODULE}"
PROC="/proc/${MODULE}"
SYSFS_LEN="/sys/class/chrdrvclass/${MODULE}/length"
SYSFS_BUF="/sys/class/chrdrvclass/${MODULE}/buffer"
PARAM_DEBUG="/sys/module/${MODULE}/parameters/debug"
TEST_IOCTL="$ROOT/checker/test_ioctl"

PASS=0
FAIL=0
SKIP=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { printf '%s\n' "$*"; }
pass() { PASS=$((PASS + 1)); printf "${GREEN}PASS${NC}: %s\n" "$1"; }
fail() { FAIL=$((FAIL + 1)); printf "${RED}FAIL${NC}: %s\n" "$1" >&2; }
skip() { SKIP=$((SKIP + 1)); printf "${YELLOW}SKIP${NC}: %s\n" "$1"; }

run_case() {
	local name=$1
	shift
	local out rc=0

	out=$("$@" 2>&1) || rc=$?
	if [[ "$rc" -eq 0 ]]; then
		pass "$name"
		return 0
	fi
	fail "$name"
	if [[ -n "$out" ]]; then
		while IFS= read -r line; do
			printf '    %s\n' "$line" >&2
		done <<< "$out"
	fi
	return 1
}

need_root() {
	if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
		log "Нужны права root: sudo make check"
		exit 1
	fi
}

# --- тесты ---

test_build() {
	if make -s kbuild >/dev/null 2>&1 && [[ -f "$KO" ]]; then
		return 0
	fi
	make kbuild
	[[ -f "$KO" ]]
}

test_ko_metadata() {
	modinfo "$KO" | grep -q "description:.*Fusy char driver" &&
		modinfo "$KO" | grep -q "version:[[:space:]]*1.0"
}

test_load_interfaces() {
	[[ -c "$DEV" ]] &&
		[[ -r "$PROC" ]] &&
		[[ -r "$SYSFS_LEN" ]] &&
		[[ -r "$SYSFS_BUF" ]] &&
		[[ -r "$PARAM_DEBUG" ]]
}

test_write_read() {
	local msg="chr_drv-autotest-$$"
	local out

	printf '%s' "$msg" >"$DEV" || return 1
	out=$(cat "$DEV") || return 1
	[[ "$out" == "$msg" ]] || return 1
	# чтение не очищает буфер — повторный cat то же самое
	out=$(cat "$DEV") || return 1
	[[ "$out" == "$msg" ]]
}

test_write_overwrites() {
	local out

	printf 'Hello' >"$DEV" || return 1
	printf 'Hi' >"$DEV" || return 1
	out=$(cat "$DEV") || return 1
	[[ "$out" == "Hi" ]]
}

test_read_eof() {
	local n
	n=$(cat "$DEV" | wc -c)
	[[ "$n" -eq 0 ]]
}

test_proc_content() {
	local proc
	proc=$(cat "$PROC") || return 1
	[[ "$proc" == *"device	chr_drv"* ]] &&
		[[ "$proc" == *"version	1.0"* ]] &&
		[[ "$proc" == *"buffer_size	256"* ]] &&
		[[ "$proc" == *"data_len	"* ]]
}

test_sysfs_after_write() {
	local msg="sysfs-check"
	local len

	printf '%s' "$msg" >"$DEV" || return 1
	len=$(cat "$SYSFS_LEN" | tr -d '\n') || return 1
	[[ "$len" == "${#msg}" ]] &&
		grep -q "$msg" "$SYSFS_BUF"
}

test_module_param_debug() {
	# module_param(bool) в sysfs: Y / N, не 0 / 1
	echo Y >"$PARAM_DEBUG" || return 1
	[[ "$(cat "$PARAM_DEBUG" | tr -d '\n')" == "Y" ]]
}

build_ioctl_test() {
	make -s -C "$ROOT/checker"
}

clear_buffer() {
	build_ioctl_test
	"$TEST_IOCTL" --clear
}

test_ioctl_program() {
	build_ioctl_test
	"$TEST_IOCTL"
}

test_reload() {
	rmmod "$MODULE" 2>/dev/null || true
	insmod "$KO" || return 1
	[[ -c "$DEV" ]]
}

# --- main ---

cleanup() {
	if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
		rmmod "$MODULE" 2>/dev/null || true
	fi
}

summary() {
	log ""
	log "======== итог ========"
	log "PASS: $PASS  FAIL: $FAIL  SKIP: $SKIP"
	if [[ "$FAIL" -gt 0 ]]; then
		exit 1
	fi
	exit 0
}

main() {
	trap cleanup EXIT
	need_root

	log "chr_drv autotests (root: ok)"
	log ""

	run_case "сборка kbuild" test_build || { summary; exit 1; }
	run_case "modinfo (description, version)" test_ko_metadata

	# загрузка: выгрузить старый экземпляр, загрузить свежий .ko
	rmmod "$MODULE" 2>/dev/null || true
	if ! insmod "$KO"; then
		fail "insmod $KO"
		dmesg | tail -5 >&2 || true
		summary
		exit 1
	fi
	pass "insmod $KO"
	build_ioctl_test

	run_case "интерфейсы /dev, /proc, /sys" test_load_interfaces

	clear_buffer
	run_case "write + read" test_write_read

	clear_buffer
	run_case "write перезаписывает с начала" test_write_overwrites

	clear_buffer
	printf '%s' "proc-data" >"$DEV"
	run_case "содержимое /proc/chr_drv" test_proc_content

	clear_buffer
	run_case "sysfs length и buffer" test_sysfs_after_write

	clear_buffer
	run_case "параметр debug в sysfs" test_module_param_debug
	run_case "ioctl (test_ioctl)" test_ioctl_program
	run_case "read после CLEAR (пустой буфер)" test_read_eof
	run_case "перезагрузка модуля (rmmod/insmod)" test_reload

	summary
}

main "$@"
