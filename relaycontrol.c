#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <hidapi/hidapi.h>

#define RELAYS                  16

//#define DEBUG

#ifdef DEBUG
#   define dprintf(fmt, args...)        printf("[%s:%d]: " fmt "\n" , __FUNCTION__, __LINE__, ## args)
#else
#   define dprintf(fmt, args...)        do{ } while(0)
#endif

#define MIN(a,b)                (((a)<=(b))?(a):(b))

#define USB_VID                 0x0416
uint16_t vendor               = USB_VID;        /* Default vendor ID */

#define USB_PID                 0x5020
uint16_t product              = USB_PID;        /* Default product ID */

int interface                 = -1;             /* Default will be first */

hid_device *handle;

typedef struct __attribute__((packed)) {
#define HID_CMD_ERASE           0x71
#define HID_CMD_READ            0xD2
#define HID_CMD_WRITE           0xC3
    uint8_t     cmd;
#define HID_CMD_LEN             ( sizeof(cmd_t) - sizeof(uint32_t) )    /* len without checksum */
    uint8_t     len;
    uint16_t    reg;
    uint16_t     _bogus[3];
#define HID_CMD_SIGNATURE       0x43444948
    uint32_t    signature;
    uint32_t    checksum;
} cmd_t;

const uint16_t relays_lut[RELAYS] = {
    0x0080, 0x0100, 0x0040, 0x0200,
    0x0020, 0x0400, 0x0010, 0x0800,
    0x0008, 0x1000, 0x0004, 0x2000,
    0x0002, 0x4000, 0x0001, 0x8000,
};

uint16_t norm_reg(uint16_t reg) {
    uint16_t    nreg = 0;
    int         i;

    for (i=0; i<RELAYS; i++)
        if (relays_lut[i] & reg)
             nreg|=1<<i;

    return nreg;
}


uint32_t CalCheckSum(uint8_t *buf, uint32_t size) {
    uint32_t    sum = 0;
    int         i = 0;

    while(size--)
        sum+=buf[i++];

    return sum;
}

enum {
    CMD_STATE = 1,
    CMD_ALL,
    CMD_NONE
};


static void usage();


uint16_t hid_get() {
    uint8_t     buf[64];
    cmd_t       *cmd;
    int         res;
    int         i;

    dprintf ("vendor=%x product=%x",vendor,product);

    cmd            = (cmd_t *)&buf;
    cmd->cmd       = HID_CMD_READ;
    cmd->len       = HID_CMD_LEN;
    cmd->signature = HID_CMD_SIGNATURE;
    cmd->reg       = 0x1111;
    cmd->_bogus[0] = 0x1111;
    cmd->_bogus[1] = 0x1111;
    cmd->_bogus[2] = 0x1111;
    cmd->checksum  = 0x0280;    /* TBD: ?? */

    res = hid_write(handle, buf, sizeof(buf));
    if (res < 0) {
        fprintf (stderr, "[%s:%d] hid_write() returned %d\n", __FUNCTION__, __LINE__, res);
        exit(1);
    }

    res = hid_read(handle, buf, sizeof(buf));
    if (res < 0) {
        fprintf (stderr, "[%s:%d] hid_read() returned %d\n", __FUNCTION__, __LINE__, res);
        exit(1);
    }

    dprintf ("returning 0x%04x",norm_reg(cmd->reg));
    return norm_reg(cmd->reg);
}


uint16_t hid_set(uint16_t reg) {
    char buf[65];
    cmd_t *cmd;
    int res;

    dprintf ("vendor=%x product=%x new_reg=0x%04x",vendor,product,reg);

    cmd            = (cmd_t *)&buf;
    cmd->cmd       = HID_CMD_WRITE;
    cmd->len       = HID_CMD_LEN;
    cmd->signature = HID_CMD_SIGNATURE;
    cmd->reg       = reg;
    cmd->_bogus[0] = 0x0;
    cmd->_bogus[1] = 0x0;
    cmd->_bogus[2] = 0x0;
    cmd->checksum  = CalCheckSum((uint8_t *)cmd,cmd->len);

    res = hid_write(handle,buf,sizeof(buf));
    if (res < 0) {
        fprintf (stderr, "[%s:%d] hid_write() returned %d\n", __FUNCTION__, __LINE__, res);
        exit(1);
    }

    return reg;
}


int parse_vpi(char * vpi) {
    int i,l,ret;

    if (vpi) {
        if (vpi[4]!=':')
            return 0;

        if (vpi[9]==':')
            interface = strtoul(vpi+10, NULL, 10);

        l=strlen(vpi);
        for (i=0;i<l;i++) {
            if (!(vpi[i] >= '0' || vpi[i] <= '9' || vpi[i]==':'))
                return 0;
            if (vpi[i]==':')
                vpi[i]=0;
        }
        vendor  = strtoul(vpi, NULL, 16);
        product = strtoul(vpi+5, NULL, 16);
        dprintf("parsing end vendor=%x product=%x int=%d",vendor,product,interface);
        return 1;
    }
    return 0;
}


int main(int argc, char *argv[]) {
    int8_t     relay_number = -1;
    int8_t     relay_state = -1;
    uint8_t     cmd = 0;
    uint16_t    reg = 0;
    struct      hid_device_info dev;
    char        path[32];
    int         i, l;

    switch (argc) {
        case 2:
            if (strcmp(argv[1],"state")==0)
                cmd = CMD_STATE;
            else if (strcmp(argv[1],"all")==0)
                cmd = CMD_ALL;
            else if (strcmp(argv[1],"none")==0)
                cmd = CMD_NONE;
            else
                usage();
            break;
        case 3:
            if (strcmp(argv[2],"state")==0)
                cmd = CMD_STATE;
            else if (strcmp(argv[2],"all")==0)
                cmd = CMD_ALL;
            else if (strcmp(argv[2],"none")==0)
                cmd = CMD_NONE;
            else {
                if (parse_vpi(argv[1]))
                    usage();

                relay_number = strtoul(argv[1], NULL, 10);
                relay_state = strtoul(argv[2], NULL, 10);
                break;
            }
            if (!parse_vpi(argv[1])) {
                fprintf (stderr, "wrong format of vendor:product:interface\n");
                return -1;
            }
            break;
        case 4:
            if (!parse_vpi(argv[1])) {
                fprintf (stderr, "wrong format of vendor:product:interface\n");
                return -1;
            }
            relay_number = strtoul(argv[2], NULL, 10);
            relay_state = strtoul(argv[3], NULL, 10);
            break;
        default:
            usage();
            return 1;
    }

    struct hid_device_info *cur;
    struct hid_device_info *devs, *cur_dev;

    if (hid_init())
        return -1;

    devs = hid_enumerate(0x0, 0x0);
    cur_dev = devs;
    while (cur_dev) {
        if (cur_dev->vendor_id == vendor || cur_dev->product_id == product) {
            if (interface >= 0) {
                if (interface == cur_dev->interface_number) {
                    strncpy(path,cur_dev->path,MIN(sizeof(path),(1+ strlen(cur_dev->path))));
                    path[sizeof(path)]=0;
                    break;
                }
            }
            else {
                strncpy(path,cur_dev->path,MIN(sizeof(path),(1+ strlen(cur_dev->path))));
                path[sizeof(path)]=0;
                break;
            }
        }
        cur_dev = cur_dev->next;
    }

    if (cur_dev) {
        dprintf("Device Found type: %04hx %04hx\n"
            "  Path: %s\n"
            "  Serial_number: %ls\n"
            "  Manufacturer: %ls\n"
            "  Product:      %ls\n"
            "  Release:      %hx\n"
            "  Interface:    %d\n",
            cur_dev->vendor_id, cur_dev->product_id, 
            cur_dev->path,
            cur_dev->serial_number,
            cur_dev->manufacturer_string,
            cur_dev->product_string,
            cur_dev->release_number,
            cur_dev->interface_number);
        hid_free_enumeration(devs);
        dprintf ("path=%s\n",path);
    }
    else {
        fprintf (stderr, "device not found\n");
        return 0;
    }

    handle = hid_open_path(path);
    if (!handle) {
        fprintf (stderr, "device not found\n");
        return 0;
    }

    printf ("ok\n");
    return 0;

    hid_set_nonblocking(handle, 0);

    if (cmd) {
        if (CMD_STATE == cmd) {
            reg = hid_get();
            printf ("current state = 0x%04x\n",reg);
        }
        else {
            if (CMD_NONE == cmd)
                reg = 0x0000;
            else if (CMD_ALL == cmd)
                reg = 0xffff;

            reg = hid_set(reg);
        }
    }
    else {
        if (relay_number < 1 || relay_number > 16 || relay_state > 1)
            usage();

        dprintf ("relay_number = %d\n", relay_number);
        dprintf ("relay_state = %d\n", relay_state);

        reg = hid_get();

        printf ("state = 0x%04x\n",reg);

        if (relay_state == 1)
            reg |= (1 << (relay_number-1));
        else
            reg &= ~(1 << (relay_number-1));

        hid_set(reg);

        reg = hid_get();

        printf ("new state = 0x%04x\n",reg);
    }

    return 0;
}

static void usage()
{
    fprintf(stderr, \
        "\nUsage:\n\n" \
        "  1. relaycontrol [<idVendor>:<idProduct>:<interface>] <relay_number> <relay_state>\n\n" \
        "       examples:\n\n" \
        "       relaycontrol 3 1                    - turn on relay 3\n" \
        "       relaycontrol 3 0                    - turn off relay 3\n\n" \
        "       relaycontrol 0416:5020:1 3 1        - turn on relay 3 in interface 1\n" \
        "       relaycontrol 0416:5020:0 3 0        - turn off relay 3 in interface 0\n\n" \
        "   2. relaycontrol [<idVendor>:<idProduct>:<interface>] <command>\n\n" \
        "       examples:\n\n" \
        "       relaycontrol state                  - get relays state\n" \
        "       relaycontrol all                    - turn on all relays\n" \
        "       relaycontrol none                   - turn off all relays\n\n" \
        "       relaycontrol 0416:5020:1 state      - get relays state on interface 1\n" \
        "       relaycontrol 0416:5020:2 none       - turn off all relays on interface 2\n\n" \
    );
    exit(1);
}
