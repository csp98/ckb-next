#include "command.h"
#include "device.h"
#include "devnode.h"
#include "firmware.h"
#include "input.h"
#include "profile.h"
#include "usb.h"

int start_kb_nrgb(usbdevice* kb, int makeactive){
    // Put the non-RGB K95 into software mode. Nothing else needs to be done hardware wise
    nk95cmd(kb, NK95_HWOFF);
    // Fill out RGB features for consistency, even though the keyboard doesn't have them
    kb->active = 1;
    kb->pollrate = -1;
    return 0;
}

// Per-key input settings and hardware actions
// 0x80 generates a normal HID interrupt, 0x40 generates a proprietary interrupt. 0xc0 generates both.
// The exceptions are the proprietary Corsair keys, which only report HID input in BIOS mode and only report Corsair input in non-BIOS mode.
// In BIOS mode, the Corsair input is disabled no matter what.
#define IN_HID          0x80
#define IN_CORSAIR      0x40

// The lower nybble controls various hardware actions
#define ACT_LIGHT       1
#define ACT_NEXT        3
#define ACT_NEXT_NOWRAP 5
#define ACT_LOCK        8
#define ACT_MR_RING     9
#define ACT_M1          10
#define ACT_M2          11
#define ACT_M3          12

int setactive_kb(usbdevice* kb, int active){
    if(NEEDS_FW_UPDATE(kb))
        return 0;
    uchar msg[3][MSG_SIZE] = {
        { 0x07, 0x04, 0 },                  // Disables or enables HW control for top row
        { 0x07, 0x40, 0 },                  // Selects key input
        { 0x07, 0x05, 2, 0, 0x03, 0x00 }    // Commits key input selection
    };
    if(active){
        pthread_mutex_lock(imutex(kb));
        kb->active = 1;
        // Clear input
        memset(&kb->input.keys, 0, sizeof(kb->input.keys));
        inputupdate(kb);
        pthread_mutex_unlock(imutex(kb));
        // Put the M-keys (K95) as well as the Brightness/Lock keys into software-controlled mode.
        msg[0][2] = 2;
        if(!usbsend(kb, msg[0], 1))
            return -1;
        DELAY_MEDIUM(kb);
        // Set input mode on the keys. They must be grouped into packets of 60 bytes (+ 4 bytes header)
        // Keys are referenced in byte pairs, with the first byte representing the key and the second byte representing the mode.
        for(int key = 0; key < N_KEYS_KB; ){
            int pair;
            for(pair = 0; pair < 30 && key < N_KEYS_KB; pair++, key++){
                // Select both standard and Corsair input. The standard input will be ignored except in BIOS mode.
                uchar action = IN_HID | IN_CORSAIR;
                // Additionally, make MR activate the MR ring
                //if(keymap_system[key].name && !strcmp(keymap_system[key].name, "mr"))
                //    action |= ACT_MR_RING;
                msg[1][4 + pair * 2] = key;
                msg[1][5 + pair * 2] = action;
            }
            // Byte 2 = pair count (usually 30, less on final message)
            msg[1][2] = pair;
            if(!usbsend(kb, msg[1], 1))
                return -1;
        }
        // Commit new input settings
        if(!usbsend(kb, msg[2], 1))
            return -1;
        DELAY_MEDIUM(kb);
    } else {
        pthread_mutex_lock(imutex(kb));
        kb->active = 0;
        // Clear input
        memset(&kb->input.keys, 0, sizeof(kb->input.keys));
        inputupdate(kb);
        pthread_mutex_unlock(imutex(kb));
        // Set the M-keys back into hardware mode, restore hardware RGB profile. It has to be sent twice for some reason.
        msg[0][2] = 1;
        if(!usbsend(kb, msg[0], 1))
            return -1;
        DELAY_MEDIUM(kb);
        if(!usbsend(kb, msg[0], 1))
            return -1;
        DELAY_MEDIUM(kb);
#ifdef OS_LINUX
        // On OSX the default key mappings are fine. On Linux, the G keys will freeze the keyboard. Set the keyboard entirely to HID input.
        for(int key = 0; key < N_KEYS_KB; ){
            int pair;
            for(pair = 0; pair < 30 && key < N_KEYS_KB; pair++, key++){
                uchar action = IN_HID;
                // Enable hardware actions
                if(keymap[key].name){
                    if(!strcmp(keymap[key].name, "mr"))
                        action = ACT_MR_RING;
                    else if(!strcmp(keymap[key].name, "m1"))
                        action = ACT_M1;
                    else if(!strcmp(keymap[key].name, "m2"))
                        action = ACT_M2;
                    else if(!strcmp(keymap[key].name, "m3"))
                        action = ACT_M3;
                    else if(!strcmp(keymap[key].name, "light"))
                        action = ACT_LIGHT;
                    else if(!strcmp(keymap[key].name, "lock"))
                        action = ACT_LOCK;
                }
                msg[1][4 + pair * 2] = key;
                msg[1][5 + pair * 2] = action;
            }
            // Byte 2 = pair count (usually 30, less on final message)
            msg[1][2] = pair;
            if(!usbsend(kb, msg[1], 1))
                return -1;
        }
        // Commit new input settings
        if(!usbsend(kb, msg[2], 1))
            return -1;
        DELAY_MEDIUM(kb);
#endif
    }
    return 0;
}

int cmd_active_kb(usbdevice* kb, usbmode* dummy1, int dummy2, int dummy3, const char* dummy4){
    return setactive_kb(kb, 1);
}

int cmd_idle_kb(usbdevice* kb, usbmode* dummy1, int dummy2, int dummy3, const char* dummy4){
    return setactive_kb(kb, 0);
}

void setmodeindex_nrgb(usbdevice *kb, int index){
    switch(index % 3){
    case 0:
        nk95cmd(kb, NK95_M1);
        break;
    case 1:
        nk95cmd(kb, NK95_M2);
        break;
    case 2:
        nk95cmd(kb, NK95_M3);
        break;
    }
}
