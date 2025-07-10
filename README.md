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
