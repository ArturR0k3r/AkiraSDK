/* HID Keyboard WASM demo */

/* Akira native HID API (provided by host runtime) */
extern int akira_hid_set_transport(int transport);
extern int akira_hid_enable(void);
extern int akira_hid_keyboard_type(const char *str);

#define HID_TRANSPORT_BLE 1

extern int ocre_sleep(int ms);

int main(void)
{
    /* Select BLE transport and enable HID */
    akira_hid_set_transport(HID_TRANSPORT_BLE);
    akira_hid_enable();

    /* Wait briefly to allow system to settle (and advertising to start) */
    ocre_sleep(1000);

    /* Type a short message to the paired host */
    akira_hid_keyboard_type("Hello from WASM!");
    return 0;
}
