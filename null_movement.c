#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <linux/input-event-codes.h>
#include <time.h>

#define MAX_KEYS 256
#define MAX_DEVICE_PATH 256
#define MAX_KEYBOARDS 10

typedef struct {
    int fd;
    char path[MAX_DEVICE_PATH];
    char name[256];
} Keyboard;

Keyboard keyboards[MAX_KEYBOARDS];
int keyboard_count = 0;

void find_keyboards() {
    DIR* dir;
    struct dirent* ent;
    char event_path[MAX_DEVICE_PATH];

    dir = opendir("/dev/input");
    if (dir == NULL) {
        perror("Error opening /dev/input");
        return;
    }

    while ((ent = readdir(dir)) != NULL && keyboard_count < MAX_KEYBOARDS) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
            snprintf(event_path, sizeof(event_path), "/dev/input/%s", ent->d_name);
            int fd = open(event_path, O_RDONLY);
            if (fd != -1) {
                char name[256];
                if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
                    if (strstr(name, "keyboard") != NULL || strstr(name, "Keyboard") != NULL) {
                        keyboards[keyboard_count].fd = fd;
                        strncpy(keyboards[keyboard_count].path, event_path, MAX_DEVICE_PATH);
                        strncpy(keyboards[keyboard_count].name, name, sizeof(name));
                        keyboard_count++;
                        printf("Found keyboard %d: %s (%s)\n", keyboard_count, name, event_path);
                    } else {
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
void setup_uinput_device(int uifd) {
    struct uinput_setup usetup;

    // Enable key events
    ioctl(uifd, UI_SET_EVBIT, EV_KEY);
    for (int i = 0; i < KEY_MAX; i++) {
        ioctl(uifd, UI_SET_KEYBIT, i);
    }

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;  // dummy vendor
    usetup.id.product = 0x5678;  // dummy product
    strcpy(usetup.name, "Null Movement Keyboard");

    ioctl(uifd, UI_DEV_SETUP, &usetup);
    ioctl(uifd, UI_DEV_CREATE);
}

const char* get_key_name(int code) {
    static char buf[32];
    switch(code) {
        case KEY_A: return "A";
        case KEY_B: return "B";
        case KEY_C: return "C";
        case KEY_D: return "D";
        case KEY_E: return "E";
        case KEY_F: return "F";
        case KEY_G: return "G";
        case KEY_H: return "H";
        case KEY_I: return "I";
        case KEY_J: return "J";
        case KEY_K: return "K";
        case KEY_L: return "L";
        case KEY_M: return "M";
        case KEY_N: return "N";
        case KEY_O: return "O";
        case KEY_P: return "P";
        case KEY_Q: return "Q";
        case KEY_R: return "R";
        case KEY_S: return "S";
        case KEY_T: return "T";
        case KEY_U: return "U";
        case KEY_V: return "V";
        case KEY_W: return "W";
        case KEY_X: return "X";
        case KEY_Y: return "Y";
        case KEY_Z: return "Z";
        case KEY_SPACE: return "SPACE";
        case KEY_ENTER: return "ENTER";
        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_LEFTSHIFT: return "LEFT SHIFT";
        case KEY_RIGHTSHIFT: return "RIGHT SHIFT";
        case KEY_LEFTCTRL: return "LEFT CTRL";
        case KEY_RIGHTCTRL: return "RIGHT CTRL";
        case KEY_LEFTALT: return "LEFT ALT";
        case KEY_RIGHTALT: return "RIGHT ALT";
        default:
            snprintf(buf, sizeof(buf), "KEY %d", code);
            return buf;
    }
}

struct timespec get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

long long timespec_diff_ns(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
}

int main() {
    int uifd;
    char key_state[MAX_KEYS] = {0};
    char last_key = 0;
    struct timespec start_time, end_time;
    struct input_event ev;
    int chosen_keyboard;

    find_keyboards();
    if (keyboard_count == 0) {
        fprintf(stderr, "No keyboards found\n");
        exit(1);
    }

    printf("Choose a keyboard (1-%d): ", keyboard_count);
    if (scanf("%d", &chosen_keyboard) != 1 || chosen_keyboard < 1 || chosen_keyboard > keyboard_count) {
        fprintf(stderr, "Invalid choice\n");
        exit(1);
    }
    chosen_keyboard--; // Adjust for 0-based index

    printf("Using keyboard: %s\n", keyboards[chosen_keyboard].name);

    uifd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uifd < 0) {
        perror("Error opening uinput");
        exit(1);
    }

    setup_uinput_device(uifd);

    printf("Null Movement Keyboard initialized. Press Ctrl+C to exit.\n");

    while (1) {
        if (read(keyboards[chosen_keyboard].fd, &ev, sizeof(struct input_event)) < 0) {
            perror("Error reading event");
            break;
        }

        if (ev.type == EV_KEY) {
            start_time = get_time();
            const char* key_name = get_key_name(ev.code);
            if (ev.value == 1) {
                printf("Key pressed: %s\n", key_name);
            } else if (ev.value == 0) {
                printf("Key released: %s\n", key_name);
            }

            if (ev.value == 1 || ev.value == 2) {  // Key pressed or held
                key_state[ev.code] = 1;
                if (last_key != 0 && last_key != ev.code) {
                    // Send key release for the previous key
                    struct input_event release_ev = {
                        .type = EV_KEY,
                        .code = last_key,
                        .value = 0
                    };
                    write(uifd, &release_ev, sizeof(struct input_event));
                    struct input_event sync_ev = {
                        .type = EV_SYN,
                        .code = SYN_REPORT,
                        .value = 0
                    };
                    write(uifd, &sync_ev, sizeof(struct input_event));
                    printf("Released previous key: %s\n", get_key_name(last_key));
                }
                last_key = ev.code;
            } else if (ev.value == 0) {  // Key released
                key_state[ev.code] = 0;
                if (ev.code == last_key) {
                    // Find the next pressed key, if any
                    last_key = 0;
                    for (int i = 0; i < MAX_KEYS; i++) {
                        if (key_state[i]) {
                            last_key = i;
                            break;
                        }
                    }
                    if (last_key != 0) {
                        // Send key press for the next key
                        struct input_event press_ev = {
                            .type = EV_KEY,
                            .code = last_key,
                            .value = 1
                        };
                        write(uifd, &press_ev, sizeof(struct input_event));
                        struct input_event sync_ev = {
                            .type = EV_SYN,
                            .code = SYN_REPORT,
                            .value = 0
                        };
                        write(uifd, &sync_ev, sizeof(struct input_event));
                        printf("Pressed next key: %s\n", get_key_name(last_key));
                    }
                }
            }
            // Send the modified event
            write(uifd, &ev, sizeof(struct input_event));
            struct input_event sync_ev = {
                .type = EV_SYN,
                .code = SYN_REPORT,
                .value = 0
            };
            write(uifd, &sync_ev, sizeof(struct input_event));

            end_time = get_time();
            long long duration_ns = timespec_diff_ns(start_time, end_time);
            printf("Processing time: %lld ns\n", duration_ns);
        }
    }

    // Clean up
    ioctl(uifd, UI_DEV_DESTROY);
    close(uifd);
    for (int i = 0; i < keyboard_count; i++) {
        close(keyboards[i].fd);
    }
    return 0;
}
