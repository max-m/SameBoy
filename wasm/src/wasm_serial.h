#ifndef wasm_serial_h
#define wasm_serial_h

typedef enum {
    SERIAL_DEVICE_NONE,
    SERIAL_DEVICE_PRINTER,
    SERIAL_DEVICE_WORKBOY,
} serial_device_t;

extern serial_device_t connected_device;

void connect_printer(unsigned index);
void connect_workboy(unsigned index);
void disconnect_serial(unsigned index);
void connect_serial(void);

#endif /* wasm_serial_h */
