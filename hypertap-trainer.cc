// Initial code nabbed from:
// https://github.com/darkelement/simplistic-examples/blob/master/evdev/evdev.c
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for asprintf */
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <vector>

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)	((array[LONG(bit)] >> OFF(bit)) & 1)

#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"

static int
is_event_device(const struct dirent *dir)
{
    return strncmp(EVENT_DEV_NAME, dir->d_name, 5) == 0;
}

static char *
scan_devices(void)
{
    struct dirent **namelist;
    int i, ndev, devnum;
    char *filename;

    ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, alphasort);
    if (ndev <= 0) {
        return NULL;
    }

    printf("Available devices:\n");

    for (i = 0; i < ndev; i++) {
        char fname[64];
        int fd = -1;
        char name[256] = "???";

        snprintf(fname, sizeof(fname), "%s/%s", DEV_INPUT_EVENT, namelist[i]->d_name);
        fd = open(fname, O_RDONLY);
        if (fd >= 0) {
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            close(fd);
            printf("%s:  %s\n", fname, name);
        }
        free(namelist[i]);
    }

    fprintf(stderr, "Select the device event number [0-%d]: ", ndev - 1);
    scanf("%d", &devnum);

    if (devnum >= ndev || devnum < 0) {
        return NULL;
    }

    asprintf(&filename, "%s/%s%d", DEV_INPUT_EVENT, EVENT_DEV_NAME, devnum);
    return filename;
}

static int
print_device_info(int fd)
{
    int i, j;
    int version;
    unsigned short id[4];
    unsigned long bit[EV_MAX][NBITS(KEY_MAX)];

    if (ioctl(fd, EVIOCGVERSION, &version)) {
        perror("can't get version");
        return 1;
    }
    printf("Input driver version is %d.%d.%d\n", 
           version >> 16, (version >> 8) & 0xff, version & 0xff);

    ioctl(fd, EVIOCGID, id);
    printf("Input device ID: bus 0x%x vendor 0x%x product 0x%x version 0x%x\n",
           id[ID_BUS], id[ID_VENDOR], id[ID_PRODUCT], id[ID_VERSION]);

    memset(bit, 0, sizeof(bit));
    ioctl(fd, EVIOCGBIT(0, EV_MAX), bit[0]);
    printf("Supported events:\n");
    for (i = 0; i < EV_MAX; i++) {
        if (test_bit(i, bit[0])) {
            printf("  Event type %d\n", i);
            if (!i) continue;
            ioctl(fd, EVIOCGBIT(i, KEY_MAX), bit[i]);
            for (j = 0; j < KEY_MAX; j++) {
                if (test_bit(j, bit[i])) {
                    printf("%d, ", j);
                }
            }
            printf("\n");
        }
    }
    return 0;
}

static uint64_t
get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ((uint64_t)ts.tv_sec) * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// axis and key codes might overlap (or at least it's not obvious if they
// may overlap)
//

struct axis_state {
    uint64_t timestamp;
    uint16_t code;
    int32_t value;
    std::vector<uint64_t> pos_presses;
    std::vector<uint64_t> neg_presses;
};

static void
print_press_stats(std::vector<uint64_t> &presses)
{
    uint64_t start = presses[0];
    uint64_t end = presses[presses.size() - 1];
    uint64_t duration = end - start;

    if (!duration) {
        printf("  single press\n");
        return;
    }

    printf("  %d presses, over %.3f seconds, avg taps/sec = %.1f\n",
           (int)presses.size(),
           (double)duration / 1e9,
           (double)presses.size() / ((double)duration / 1e9)
           );

    printf("  detailed tap rates (as implied taps/sec):\n");
    printf("  ");
    for (int i = 1; i < presses.size(); i++) {
        uint64_t tap_delta = presses[i] - presses[i-1];
        printf("%-7.1f", 1e9 / (double)tap_delta);
        if (i % 10 == 0)
            printf("\n  ");
    }
    printf("\n");
}

static int
print_events(int fd)
{
    struct input_event ev;
    unsigned int size;

    printf("Testing ... (interrupt to exit)\n");

    std::vector<struct axis_state> axis_state;
    axis_state.resize(UINT16_MAX);

    std::vector<struct axis_state> key_state;
    key_state.resize(UINT16_MAX);

    uint64_t start_time = 0;

    while (1) {

        size = read(fd, &ev, sizeof(struct input_event));

        if (size < sizeof(struct input_event)) {
            printf("expected %u bytes, got %u\n", sizeof(struct input_event), size);
            perror("\nerror reading");
            return EXIT_FAILURE;
        }

        uint64_t event_time =
            ((uint64_t)ev.time.tv_sec) * 1000000000ULL +
            (uint64_t)ev.time.tv_usec * 1000ULL;

        if (!start_time) {
            start_time = event_time;
            printf("MASH!!!!\n");
        } else {
#if 0
            if (event_time - start_time > (uint64_t)60e9) {
                printf("Stop!\n");
                break;
            }
#endif
        }

#if 0
        printf("Event: time %ld.%06ld, ", ev.time.tv_sec, ev.time.tv_usec);
        switch (ev.type) {
        case EV_SYN:
            printf("-------------- SYN_REPORT ------------\n");
            break;
        default:
            printf("type: %i, code: %i, value: %i\n", ev.type, ev.code, ev.value);
            break;
        }
#endif

        if (ev.type == EV_KEY) {
            if (key_state[ev.code].timestamp) {
                if (key_state[ev.code].value && ev.value == 0) {
                    key_state[ev.code].pos_presses.push_back(event_time);
                    double hz = 0;
                    int n_presses = key_state[ev.code].pos_presses.size();
                    if (n_presses > 1) {
                        uint64_t tap_delta = 
                            key_state[ev.code].pos_presses[n_presses-1] -
                            key_state[ev.code].pos_presses[n_presses-2];
                        hz = 1e9 / (double)tap_delta;
                    }
                    printf("key press %d, n=%d, %5.3f Hz\n",
                           ev.code,
                           key_state[ev.code].pos_presses.size(),
                           hz);
                }
            }

            key_state[ev.code].timestamp = event_time;
            key_state[ev.code].code = ev.code;
            key_state[ev.code].value = ev.value;
        }

        if (ev.type == EV_ABS) {
            if (axis_state[ev.code].timestamp) {
                if (axis_state[ev.code].value > 0  && ev.value == 0) {
                    axis_state[ev.code].pos_presses.push_back(event_time);
                    double hz = 0;
                    int n_presses = axis_state[ev.code].pos_presses.size();
                    if (n_presses > 1) {
                        uint64_t tap_delta = 
                            axis_state[ev.code].pos_presses[n_presses-1] -
                            axis_state[ev.code].pos_presses[n_presses-2];
                        hz = 1e9 / (double)tap_delta;
                    }
                    printf("axis %d press, n=%3d, %5.3f Hz\n",
                           ev.code,
                           axis_state[ev.code].neg_presses.size(),
                           hz);
                } else if (axis_state[ev.code].value < 0  && ev.value == 0) {
                    axis_state[ev.code].neg_presses.push_back(event_time);
                    double hz = 0;
                    int n_presses = axis_state[ev.code].neg_presses.size();
                    if (n_presses > 1) {
                        uint64_t tap_delta = 
                            axis_state[ev.code].neg_presses[n_presses-1] -
                            axis_state[ev.code].neg_presses[n_presses-2];
                        hz = 1e9 / (double)tap_delta;
                    }
                    printf("axis %d (neg) press, n=%3d, %5.3f Hz\n",
                           ev.code,
                           axis_state[ev.code].neg_presses.size(),
                           hz);
                }
            }

            axis_state[ev.code].timestamp = event_time;
            axis_state[ev.code].code = ev.code;
            axis_state[ev.code].value = ev.value;
        }
    }

    for (int i = 0; i < key_state.size(); i++) {
        if (key_state[i].pos_presses.size()) {
            printf("Key %d:\n", key_state[i].code);
            print_press_stats(key_state[i].pos_presses);
        }
    }
    printf("\n");
    for (int i = 0; i < axis_state.size(); i++) {
        if (!axis_state[i].timestamp)
            continue;

        if (axis_state[i].neg_presses.size()) {
            printf("Axis %d (neg):\n", axis_state[i].code);
            print_press_stats(axis_state[i].neg_presses);
        }
        if (axis_state[i].pos_presses.size()) {
            printf("Axis %d (pos):\n", axis_state[i].code);
            print_press_stats(axis_state[i].pos_presses);
        }
    }

    return 0;
}

int
main (int argc, char **argv)
{
    int fd, grabbed;
    char *filename;

    if (getuid() != 0) {
        fprintf(stderr, "Not running as root, no devices may be available.\n");
    }

    filename = scan_devices();
    if (!filename) {
        fprintf(stderr, "Device not found\n");
        return EXIT_FAILURE;
    }

    if ((fd = open(filename, O_RDONLY)) < 0) {
        perror("");
        if (errno == EACCES && getuid() != 0) {
            fprintf(stderr, "You do not have access to %s. Try "
                    "running as root instead.\n", filename);
        }
        return EXIT_FAILURE;
    }

    free(filename);

    if (print_device_info(fd)) {
        return EXIT_FAILURE;
    }

    grabbed = ioctl(fd, EVIOCGRAB, (void *) 1);
    ioctl(fd, EVIOCGRAB, (void *) 0);
    if (grabbed) {
        printf("This device is grabbed by another process. Try switching VT.\n");
        return EXIT_FAILURE;
    }

    return print_events(fd);
}

