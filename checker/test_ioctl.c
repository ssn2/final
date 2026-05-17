/*
 * test_ioctl.c — userspace-тесты ioctl для chr_drv.
 * Возврат: 0 — все проверки прошли, иначе количество ошибок (макс. 125).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHR_DRV_DEV		"/dev/chr_drv"
#define CHR_DRV_IOC_MAGIC	'c'
#define CHR_DRV_IOC_CLEAR	_IO(CHR_DRV_IOC_MAGIC, 0)
#define CHR_DRV_IOC_GET_LEN	_IOR(CHR_DRV_IOC_MAGIC, 1, size_t)
#define CHR_DRV_IOC_GET_DEBUG	_IOR(CHR_DRV_IOC_MAGIC, 2, int)
#define CHR_DRV_IOC_SET_DEBUG	_IOW(CHR_DRV_IOC_MAGIC, 3, int)

static int failures;

static void fail(const char *msg)
{
	fprintf(stderr, "FAIL: %s\n", msg);
	if (failures < 125)
		failures++;
}

static int do_clear(int fd)
{
	if (ioctl(fd, CHR_DRV_IOC_CLEAR, NULL) < 0) {
		perror("ioctl CLEAR");
		return -1;
	}
	return 0;
}

static void run_ioctl_tests(int fd)
{
	const char payload[] = "ioctl-test";
	size_t len = 0;
	int dbg = -1;

	if (do_clear(fd) < 0) {
		fail("ioctl CLEAR (initial)");
		return;
	}

	if (write(fd, payload, sizeof(payload) - 1) !=
	    (ssize_t)(sizeof(payload) - 1)) {
		fail("write payload");
		return;
	}
	if (ioctl(fd, CHR_DRV_IOC_GET_LEN, &len) < 0) {
		fail("ioctl GET_LEN");
		return;
	}
	if (len != sizeof(payload) - 1)
		fail("GET_LEN mismatch after write");

	if (do_clear(fd) < 0) {
		fail("ioctl CLEAR");
		return;
	}

	len = 99;
	if (ioctl(fd, CHR_DRV_IOC_GET_LEN, &len) < 0) {
		fail("ioctl GET_LEN after clear");
		return;
	}
	if (len != 0)
		fail("GET_LEN not zero after CLEAR");

	dbg = 1;
	if (ioctl(fd, CHR_DRV_IOC_SET_DEBUG, &dbg) < 0) {
		fail("ioctl SET_DEBUG=1");
		return;
	}

	dbg = -1;
	if (ioctl(fd, CHR_DRV_IOC_GET_DEBUG, &dbg) < 0) {
		fail("ioctl GET_DEBUG");
		return;
	}
	if (dbg != 1)
		fail("GET_DEBUG expected 1");

	dbg = 0;
	if (ioctl(fd, CHR_DRV_IOC_SET_DEBUG, &dbg) < 0) {
		fail("ioctl SET_DEBUG=0");
		return;
	}
	if (ioctl(fd, CHR_DRV_IOC_GET_DEBUG, &dbg) < 0 || dbg != 0)
		fail("GET_DEBUG expected 0");

	if (ioctl(fd, _IO(CHR_DRV_IOC_MAGIC, 99), NULL) == 0) {
		fail("invalid ioctl should fail");
	} else if (errno != ENOTTY) {
		fail("invalid ioctl errno (expected ENOTTY)");
	}
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;

	fd = open(CHR_DRV_DEV, O_RDWR);
	if (fd < 0) {
		perror("open " CHR_DRV_DEV);
		return 1;
	}

	if (argc > 1 && strcmp(argv[1], "--clear") == 0) {
		ret = do_clear(fd);
		close(fd);
		return ret < 0 ? 1 : 0;
	}

	run_ioctl_tests(fd);
	close(fd);

	if (failures)
		fprintf(stderr, "ioctl tests: %d failure(s)\n", failures);
	return failures ? failures : 0;
}
