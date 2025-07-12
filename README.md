# timotwofx_driver

This is the Linux kernel driver code for the TimoTwoFX module by LumenRadio.

Currently, the driver supports read/write access to all registers documented in the LumenRadio technical documentation.

Note: DMX/RDM functionality is not supported yet.

How to use

✅ Write to a register

To write data to a register from user space, use the standard write() syscall.

✅ Read from a register

To read data from a register:

First, send the request frame using the write() syscall.

Then, retrieve the response using the read() syscall.

⚠️ Note

If you need to access IRQ flags after sending a command, call read() immediately after write().

# Importings things:

* In the read function, the structure of the data is as follows:
    [irq_flags][data_0][data_1] ... [data_n-1],
    where n is the size of the register being read.
    (See the official documentation: https://docs.lumenrad.io/timotwo/)

* When you call write() to send a frame and receive -1 as a return value, it likely indicates an error occurred during frame transmission.
    I recommend calling read() immediately afterward to retrieve the irq_flags, which may help identify the reason for the error.