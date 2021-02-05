#include "Esp32I2C.hxx"
#include <driver/i2c.h>
#include <esp_err.h>
#include <esp_vfs.h>
#include <utils/logging.h>

static constexpr TickType_t I2C_WRITE_TIMEOUT = pdMS_TO_TICKS(10);
static constexpr uint32_t I2C_BUS_SPEED = 100000UL;

static ssize_t i2c_vfs_write(int fd, const void *buf, size_t size)
{
    return Singleton<Esp32I2C>::instance()->write(fd, buf, size);
}

static ssize_t i2c_vfs_read(int fd, void *buf, size_t size)
{
    return Singleton<Esp32I2C>::instance()->read(fd, buf, size);
}

static int i2c_vfs_open(const char *path, int flags, int mode)
{
    return Singleton<Esp32I2C>::instance()->open(path, flags, mode);
}

static int i2c_vfs_close(int fd)
{
    return Singleton<Esp32I2C>::instance()->close(fd);
}

static int i2c_vfs_ioctl(int fd, int cmd, va_list args)
{
    return Singleton<Esp32I2C>::instance()->ioctl(fd, cmd, args);
}

Esp32I2C::Esp32I2C(uint8_t sda, uint8_t scl, const char *path)
    : sda_(static_cast<gpio_num_t>(sda))
    , scl_(static_cast<gpio_num_t>(scl))
    , vfsPath_(path)
    , devices_(0)
{

}

void Esp32I2C::init()
{
    esp_vfs_t vfs;
    memset(&vfs, 0, sizeof(vfs));
    vfs.flags = ESP_VFS_FLAG_DEFAULT;
    vfs.ioctl = i2c_vfs_ioctl;
    vfs.open = i2c_vfs_open;
    vfs.close = i2c_vfs_close;
    vfs.write = i2c_vfs_write;
    vfs.read = i2c_vfs_read;
    vfs.flags = ESP_VFS_FLAG_DEFAULT;

    LOG(INFO, "[I2C] Registering %s VFS interface", vfsPath_);
    ESP_ERROR_CHECK(esp_vfs_register(vfsPath_, &vfs, this));

    i2c_config_t i2c_config;
    bzero(&i2c_config, sizeof(i2c_config_t));
    i2c_config.mode = I2C_MODE_MASTER;
    i2c_config.sda_io_num = sda_;
    i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.scl_io_num = scl_;
    i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.master.clk_speed = I2C_BUS_SPEED;
    LOG(INFO, "[I2C] Configuring I2C Master");
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
}

int Esp32I2C::open(const char *path, int flags, int mode)
{
    LOG(VERBOSE, "[I2C] Opening %s (fd: %d)", path, devices_);
    addr_[devices_] = 0;
    return devices_++;
}

int Esp32I2C::close(int fd)
{
    LOG(VERBOSE, "[I2C fd:%d] Closing", fd);
    addr_[fd] = -1;
    return 0;
}

int Esp32I2C::ioctl(int fd, int cmd, va_list args)
{
    if (IOC_TYPE(cmd) != I2C_MAGIC)
    {
        // unsupported ioctl operation
        errno = EINVAL;
        return -1;
    }
    switch (cmd)
    {
        case I2C_SLAVE:
            // store the address for read/write operations
            addr_[fd] = reinterpret_cast<uintptr_t>(va_arg(args, uintptr_t));
        break;
        case I2C_RDWR:
            struct i2c_rdwr_ioctl_data * data =
                reinterpret_cast<struct i2c_rdwr_ioctl_data *>(va_arg(args, struct i2c_rdwr_ioctl_data *));
            for (size_t idx = 0; idx < data->nmsgs; idx++)
            {
                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                i2c_master_start(cmd);
                if (data->msgs[idx].flags & I2C_M_RD)
                {
                    i2c_master_write_byte(cmd, (data->msgs[idx].addr << 1) |
                                               I2C_MASTER_READ, true);
                    if (data->msgs[idx].len > 1)
                    {
                        i2c_master_read(cmd, data->msgs[idx].buf
                                      , data->msgs[idx].len - 1
                                      , I2C_MASTER_ACK);
                    }
                    i2c_master_read_byte(cmd, data->msgs[idx].buf +
                                              data->msgs[idx].len - 1
                                       , I2C_MASTER_NACK);
                }
                else
                {
                    i2c_master_write_byte(cmd, (data->msgs[idx].addr << 1) |
                                                I2C_MASTER_WRITE, true);
                    i2c_master_write(cmd, data->msgs[idx].buf
                                   , data->msgs[idx].len, true);
                }
                i2c_master_stop(cmd);
                esp_err_t ret =
                    i2c_master_cmd_begin(I2C_NUM_0, cmd, I2C_WRITE_TIMEOUT);
                i2c_cmd_link_delete(cmd);
                if (ret != ESP_OK)
                {
                    LOG_ERROR("[I2C fd:%d] I2C transaction failure: %s", fd
                            , esp_err_to_name(ret));
                    errno = ETIMEDOUT;
                    return -1;
                }
                LOG(VERBOSE
                  , "[I2C fd:%d, addr:%d] I2C transaction success: %s", fd
                  , data->msgs[idx].addr, esp_err_to_name(ret));
            }
        break;
    }
    return 0;
}

ssize_t Esp32I2C::write(int fd, const void *buf, size_t size)
{
    if (addr_[fd] > 0)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr_[fd] << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, (uint8_t *)buf, size, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, I2C_WRITE_TIMEOUT);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK)
        {
            LOG(VERBOSE, "[I2C fd:%d, addr:%d] I2C transaction success: %s", fd
              , addr_[fd], esp_err_to_name(ret));
            return 1;
        }
        LOG_ERROR("[I2C fd:%d, addr:%d] I2C transaction failure: %s", fd
                , addr_[fd], esp_err_to_name(ret));
        errno = ETIMEDOUT;
    }
    else
    {
        LOG_ERROR("[I2C fd:%d] no address has been assigned, EINVAL", fd);
        errno = EINVAL;
    }
    return 0;
}

ssize_t Esp32I2C::read(int fd, void *buf, size_t size)
{
    if (addr_[fd] > 0)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr_[fd] << 1) | I2C_MASTER_READ, true);
        if (size > 1)
        {
            i2c_master_read(cmd, (uint8_t *)buf, size - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, (uint8_t *)buf + size - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, I2C_WRITE_TIMEOUT);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK)
        {
            LOG(VERBOSE, "[I2C fd:%d, addr:%d] I2C transaction success: %s", fd
              , addr_[fd], esp_err_to_name(ret));
            return size;
        }
        LOG_ERROR("[I2C fd:%d] I2C transaction failure: %s", fd
                , esp_err_to_name(ret));
        errno = ETIMEDOUT;
    }
    else
    {
        LOG_ERROR("[I2C fd:%d] no address has been assigned, EINVAL", fd);
        errno = EINVAL;
    }
    return 0;
}