#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <linux/input-event-codes.h>
#include <errno.h>
#include <sys/select.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
#include <libevdev/libevdev.h>

#define MAX_KEYS 256
#define MAX_DEVICE_PATH 256
#define MAX_KEYBOARDS 5

typedef struct {
    int fd;
    struct libevdev *evdev;
} Keyboard;

static Keyboard keyboards[MAX_KEYBOARDS];
static int keyboard_count = 0;
static int uifd;
static char key_state[MAX_KEYS] = {0};
static char last_key = 0;
static volatile sig_atomic_t keep_running = 1;

static inline int is_keyboard(struct libevdev *dev) {
    return libevdev_has_event_type(dev, EV_KEY) &&
           libevdev_has_event_code(dev, EV_KEY, KEY_A) &&
           libevdev_has_event_code(dev, EV_KEY, KEY_Z);
}

static void find_keyboards(void) {
    DIR* dir = opendir("/dev/input");
    if (!dir) return;

    struct dirent* ent;
    char event_path[MAX_DEVICE_PATH];

    while ((ent = readdir(dir)) != NULL && keyboard_count < MAX_KEYBOARDS) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
            if (snprintf(event_path, sizeof(event_path), "/dev/input/%s", ent->d_name) >= sizeof(event_path)) {
                fprintf(stderr, "Event path too long: %s\n", ent->d_name);
                continue;
            }
            int fd = open(event_path, O_RDONLY);
            if (fd != -1) {
                struct libevdev *evdev = NULL;
                if (libevdev_new_from_fd(fd, &evdev) == 0) {
                    if (is_keyboard(evdev)) {
                        keyboards[keyboard_count].fd = fd;
                        keyboards[keyboard_count].evdev = evdev;
                        keyboard_count++;
                        printf("Found keyboard: %s\n", event_path);
                    } else {
                        libevdev_free(evdev);
                        close(fd);
                    }
                } else {
                    close(fd);
                }
            }
        }
    }
    closedir(dir);
}

static void setup_uinput_device(void) {
    struct uinput_setup usetup;

    ioctl(uifd, UI_SET_EVBIT, EV_KEY);
    for (int i = 0; i < KEY_MAX; i++) {
        ioctl(uifd, UI_SET_KEYBIT, i);
    }

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "Null Movement Keyboard");

    ioctl(uifd, UI_DEV_SETUP, &usetup);
    ioctl(uifd, UI_DEV_CREATE);
}

static const char* get_key_name(int code) {
    static const char* key_names[] = {
        [KEY_A] = "A", [KEY_B] = "B", [KEY_C] = "C", [KEY_D] = "D", [KEY_E] = "E",
        [KEY_F] = "F", [KEY_G] = "G", [KEY_H] = "H", [KEY_I] = "I", [KEY_J] = "J",
        [KEY_K] = "K", [KEY_L] = "L", [KEY_M] = "M", [KEY_N] = "N", [KEY_O] = "O",
        [KEY_P] = "P", [KEY_Q] = "Q", [KEY_R] = "R", [KEY_S] = "S", [KEY_T] = "T",
        [KEY_U] = "U", [KEY_V] = "V", [KEY_W] = "W", [KEY_X] = "X", [KEY_Y] = "Y",
        [KEY_Z] = "Z", [KEY_SPACE] = "SPACE", [KEY_ENTER] = "ENTER",
        [KEY_BACKSPACE] = "BACKSPACE", [KEY_LEFTSHIFT] = "LEFT SHIFT",
        [KEY_RIGHTSHIFT] = "RIGHT SHIFT", [KEY_LEFTCTRL] = "LEFT CTRL",
        [KEY_RIGHTCTRL] = "RIGHT CTRL", [KEY_LEFTALT] = "LEFT ALT",
        [KEY_RIGHTALT] = "RIGHT ALT"
    };
    return (code < sizeof(key_names)/sizeof(key_names[0]) && key_names[code]) ? key_names[code] : "UNKNOWN";
}

static inline void write_event(int type, int code, int value) {
    struct input_event ev = {.type = type, .code = code, .value = value};
    ssize_t bytes_written = write(uifd, &ev, sizeof(ev));
    if (bytes_written != sizeof(ev)) {
        fprintf(stderr, "Failed to write event: %s\n", strerror(errno));
    }
}

static void signal_handler(int signum) {
    keep_running = 0;
}

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    umask(0);
    if (chdir("/") < 0) {
        fprintf(stderr, "Failed to change directory\n");
        exit(EXIT_FAILURE);
    }
    for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        close(x);
    }
    openlog("null_movement_keyboard", LOG_PID, LOG_DAEMON);
}

int main(void) {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    printf("Null Movement Keyboard starting\n");

    find_keyboards();
    if (keyboard_count == 0) {
        fprintf(stderr, "No keyboards found\n");
        return 1;
    }

    uifd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uifd < 0) {
        fprintf(stderr, "Error opening uinput: %s\n", strerror(errno));
        return 1;
    }

    setup_uinput_device();

    fd_set readfds;
    int max_fd = -1;
    struct input_event ev;
    int click_count = 0;

    printf("Press any key 10 times to start daemonization...\n");

    while (keep_running) {
        FD_ZERO(&readfds);
        for (int i = 0; i < keyboard_count; i++) {
            FD_SET(keyboards[i].fd, &readfds);
            if (keyboards[i].fd > max_fd) max_fd = keyboards[i].fd;
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0 && errno != EINTR) break;

        if (!keep_running) break;

        for (int i = 0; i < keyboard_count; i++) {
            if (FD_ISSET(keyboards[i].fd, &readfds)) {
                int rc;
                while ((rc = libevdev_next_event(keyboards[i].evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev)) == 0) {
                    if (ev.type == EV_KEY) {
                        const char* key_name = get_key_name(ev.code);
                        if (ev.value == 1) {
                            printf("Key pressed: %s\n", key_name);
                            click_count++;
                            if (click_count == 10) {
                                printf("Daemonizing...\n");
                                daemonize();
                                syslog(LOG_INFO, "Null Movement Keyboard daemon started");
                            }
                            key_state[ev.code] = 1;
                            if (last_key && last_key != ev.code) {
                                write_event(EV_KEY, last_key, 0);
                                write_event(EV_SYN, SYN_REPORT, 0);
                            }
                            last_key = ev.code;
                        } else if (ev.value == 0) {
                            printf("Key released: %s\n", key_name);
                            key_state[ev.code] = 0;
                            if (ev.code == last_key) {
                                last_key = 0;
                                for (int j = 0; j < MAX_KEYS; j++) {
                                    if (key_state[j]) {
                                        last_key = j;
                                        write_event(EV_KEY, last_key, 1);
                                        write_event(EV_SYN, SYN_REPORT, 0);
                                        break;
                                    }
                                }
                            }
                        }
                        write_event(ev.type, ev.code, ev.value);
                        write_event(EV_SYN, SYN_REPORT, 0);
                    }
                }
                if (rc != -EAGAIN && rc != -EINTR) {
                    fprintf(stderr, "Error reading event: %s\n", strerror(-rc));
                }
            }
        }
    }

    printf("Null Movement Keyboard shutting down\n");

    ioctl(uifd, UI_DEV_DESTROY);
    close(uifd);
    for (int i = 0; i < keyboard_count; i++) {
        libevdev_free(keyboards[i].evdev);
        close(keyboards[i].fd);
    }

    return 0;
}
