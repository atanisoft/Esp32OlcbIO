#include "i2c.h"
#include "i2c-dev.h"
#include <driver/gpio.h>
#include <utils/Singleton.hxx>

class Esp32I2C : public Singleton<Esp32I2C>
{
public:
    Esp32I2C(uint8_t sda, uint8_t scl, const char *path = "/dev/i2c");
    void init();

    int open(const char *path, int flags, int mode);
    int close(int fd);
    int ioctl(int fd, int cmd, va_list args);
    ssize_t write(int fd, const void *buf, size_t size);
    ssize_t read(int fd, void *buf, size_t size);
private:
    const gpio_num_t sda_;
    const gpio_num_t scl_;
    const char *vfsPath_;
    std::map<uint8_t, uint8_t> addr_;
    uint8_t devices_;
};