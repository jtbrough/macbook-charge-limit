// SPDX-License-Identifier: MIT
/*
 * Intel MacBook SMC charge limit helper for Linux.
 *
 * Supported SMC keys:
 *   BCLM - Battery Charge Level Max
 *   BFCL - Battery final charge level on tested Intel MacBook firmware
 *
 * Run as root. The program uses direct I/O port access to the Apple SMC.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define APPLESMC_DATA_PORT 0x300
#define APPLESMC_CMD_PORT 0x304
#define APPLESMC_NR_PORTS 32

#define SMC_STATUS_AWAITING_DATA 0x01
#define SMC_STATUS_IB_CLOSED 0x02
#define SMC_STATUS_BUSY 0x04

#define APPLESMC_MIN_WAIT_US 8
#define APPLESMC_READ_CMD 0x10
#define APPLESMC_WRITE_CMD 0x11

static int read_first_line(const char *path, char *buf, size_t len)
{
    FILE *file;
    size_t used;

    if (len == 0)
        return -EINVAL;

    file = fopen(path, "r");
    if (!file)
        return -errno;

    if (!fgets(buf, len, file)) {
        int ret = ferror(file) ? -errno : -ENODATA;
        fclose(file);
        return ret;
    }

    fclose(file);

    used = strcspn(buf, "\r\n");
    buf[used] = '\0';
    return 0;
}

static int starts_with(const char *value, const char *prefix)
{
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

static int check_supported_machine(void)
{
    char vendor[128];
    char product[128];
    int ret;

    ret = read_first_line("/sys/class/dmi/id/sys_vendor", vendor, sizeof(vendor));
    if (ret) {
        fprintf(stderr, "Failed to read DMI system vendor: %s\n", strerror(-ret));
        return ret;
    }

    ret = read_first_line("/sys/class/dmi/id/product_name", product, sizeof(product));
    if (ret) {
        fprintf(stderr, "Failed to read DMI product name: %s\n", strerror(-ret));
        return ret;
    }

    if (strcmp(vendor, "Apple Inc.") != 0 || !starts_with(product, "MacBook")) {
        fprintf(stderr,
                "Refusing to access Apple SMC ports on unsupported hardware: "
                "vendor=\"%s\" product=\"%s\"\n",
                vendor, product);
        return -ENODEV;
    }

    return 0;
}

static int wait_status(uint8_t val, uint8_t mask)
{
    int us = APPLESMC_MIN_WAIT_US;

    for (int i = 0; i < 24; i++) {
        uint8_t status = inb(APPLESMC_CMD_PORT);
        if ((status & mask) == val)
            return 0;

        usleep(us);
        if (i > 9)
            us <<= 1;
    }

    return -EIO;
}

static int send_byte(uint8_t byte, uint16_t port)
{
    int ret = wait_status(0, SMC_STATUS_IB_CLOSED);
    if (ret)
        return ret;

    ret = wait_status(SMC_STATUS_BUSY, SMC_STATUS_BUSY);
    if (ret)
        return ret;

    outb(byte, port);
    return 0;
}

static int send_command(uint8_t cmd)
{
    int ret = wait_status(0, SMC_STATUS_IB_CLOSED);
    if (ret)
        return ret;

    outb(cmd, APPLESMC_CMD_PORT);
    return 0;
}

static int smc_sane(void)
{
    int ret = wait_status(0, SMC_STATUS_BUSY);
    if (!ret)
        return 0;

    ret = send_command(APPLESMC_READ_CMD);
    if (ret)
        return ret;

    return wait_status(0, SMC_STATUS_BUSY);
}

static int send_key(const char *key)
{
    for (int i = 0; i < 4; i++) {
        int ret = send_byte((uint8_t)key[i], APPLESMC_DATA_PORT);
        if (ret)
            return ret;
    }

    return 0;
}

static int smc_read_ui8(const char *key, uint8_t *value)
{
    int ret = smc_sane();
    if (ret)
        return ret;

    ret = send_command(APPLESMC_READ_CMD);
    if (ret)
        return ret;

    ret = send_key(key);
    if (ret)
        return ret;

    ret = send_byte(1, APPLESMC_DATA_PORT);
    if (ret)
        return ret;

    ret = wait_status(SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY,
                      SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY);
    if (ret)
        return ret;

    *value = inb(APPLESMC_DATA_PORT);

    for (int i = 0; i < 16; i++) {
        usleep(APPLESMC_MIN_WAIT_US);
        if (!(inb(APPLESMC_CMD_PORT) & SMC_STATUS_AWAITING_DATA))
            break;
        (void)inb(APPLESMC_DATA_PORT);
    }

    return wait_status(0, SMC_STATUS_BUSY);
}

static int smc_write_ui8(const char *key, uint8_t value)
{
    int ret = smc_sane();
    if (ret)
        return ret;

    ret = send_command(APPLESMC_WRITE_CMD);
    if (ret)
        return ret;

    ret = send_key(key);
    if (ret)
        return ret;

    ret = send_byte(1, APPLESMC_DATA_PORT);
    if (ret)
        return ret;

    ret = send_byte(value, APPLESMC_DATA_PORT);
    if (ret)
        return ret;

    return wait_status(0, SMC_STATUS_BUSY);
}

static int parse_percent(const char *arg)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(arg, &end, 10);

    if (errno || !end || *end != '\0' || value < 20 || value > 100) {
        fprintf(stderr, "Expected a percent from 20 to 100, got: %s\n", arg);
        return -1;
    }

    return (int)value;
}

static int acquire_io(void)
{
    int ret = check_supported_machine();
    if (ret)
        return -1;

    if (ioperm(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS, 1) != 0) {
        perror("ioperm");
        fprintf(stderr, "Run as root; direct SMC I/O requires CAP_SYS_RAWIO.\n");
        return -1;
    }

    return 0;
}

static int read_limits(void)
{
    struct smc_key {
        const char *name;
        uint8_t value;
    } keys[] = {
        { "BCLM", 0 },
        { "BFCL", 0 },
    };

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        int ret = smc_read_ui8(keys[i].name, &keys[i].value);
        if (ret) {
            fprintf(stderr, "Failed to read %s: %s\n", keys[i].name,
                    strerror(-ret));
            return ret;
        }
    }

    printf("BCLM=%u BFCL=%u\n", keys[0].value, keys[1].value);
    return 0;
}

static int set_limit(uint8_t percent)
{
    const char *keys[] = { "BCLM", "BFCL" };

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        int ret = smc_write_ui8(keys[i], percent);
        if (ret) {
            fprintf(stderr, "Failed to write %s: %s\n", keys[i],
                    strerror(-ret));
            return ret;
        }
    }

    return read_limits();
}

static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "Usage:\n"
            "  %s read\n"
            "  %s set <20-100>\n"
            "  %s reset\n\n"
            "Examples:\n"
            "  sudo %s read\n"
            "  sudo %s set 80\n"
            "  sudo %s reset\n",
            argv0, argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv)
{
    int ret;

    if (argc < 2) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "help") == 0 ||
        strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        if (argc != 2) {
            usage(stderr, argv[0]);
            return 2;
        }
        usage(stdout, argv[0]);
        return 0;
    } else if (strcmp(argv[1], "read") == 0) {
        if (argc != 2) {
            usage(stderr, argv[0]);
            return 2;
        }
        if (acquire_io() != 0)
            return 1;
        ret = read_limits();
    } else if (strcmp(argv[1], "set") == 0) {
        int percent;
        if (argc != 3) {
            usage(stderr, argv[0]);
            return 2;
        }
        percent = parse_percent(argv[2]);
        if (percent < 0)
            return 2;
        if (acquire_io() != 0)
            return 1;
        ret = set_limit((uint8_t)percent);
    } else if (strcmp(argv[1], "reset") == 0) {
        if (argc != 2) {
            usage(stderr, argv[0]);
            return 2;
        }
        if (acquire_io() != 0)
            return 1;
        ret = set_limit(100);
    } else {
        usage(stderr, argv[0]);
        return 2;
    }

    return ret == 0 ? 0 : 1;
}
