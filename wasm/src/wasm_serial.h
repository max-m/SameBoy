#ifndef wasm_serial_h
#define wasm_serial_h

typedef enum {
    SERIAL_DEVICE_NONE,
    SERIAL_DEVICE_PRINTER,
    SERIAL_DEVICE_WORKBOY,
} serial_device_t;

extern serial_device_t connected_device;

#endif /* wasm_serial_h */
