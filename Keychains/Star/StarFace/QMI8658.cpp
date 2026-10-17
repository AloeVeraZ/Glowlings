// QMI8658 interface used by Star: checked raw samples, temperature and WoM setup.
#include "QMI8658.h"

static constexpr uint8_t ADDRESS_LOW = 0x6a, ADDRESS_HIGH = 0x6b;
static uint8_t QMI8658_slave_addr = ADDRESS_LOW;
static bool rawReadLocked = false;
void QMI8658_setRawReadLock(bool enabled) { rawReadLocked = enabled; }

unsigned char QMI8658_write_reg(unsigned char reg, unsigned char value) {
    // The upstream retry loop never updated ret, so every command (including
    // CTRL9) was sent five times. One I2C transaction is the intended behavior.
    DEV_I2C_Write_Byte(QMI8658_slave_addr, reg, value);
    return 1;
}

unsigned char QMI8658_read_reg(unsigned char reg, unsigned char *buf, unsigned short len) {
    DEV_I2C_Read_nByte(QMI8658_slave_addr, reg, buf, len);
    return 1;
}

float QMI8658_readTemp(void) {
    unsigned char buf[2];
    short temp = 0;
    float temp_f = 0;

    QMI8658_read_reg(QMI8658Register_Tempearture_L, buf, 2);
    temp = ((short)buf[1] << 8) | buf[0];
    temp_f = (float)temp / 256.0f;

    return temp_f;
}

bool QMI8658_read_xyz_raw(short raw_acc_xyz[3], short raw_gyro_xyz[3], unsigned int *tim_count) {
    if (rawReadLocked) {
        Wire.beginTransmission(QMI8658_slave_addr);
        Wire.write(QMI8658Register_StatusInt);
        if (Wire.endTransmission(false) != 0 ||
            Wire.requestFrom(QMI8658_slave_addr, (uint8_t)1) != 1)
            return false;
        unsigned char status = Wire.read();
        if ((status & 0x03) != 0x01 && (status & 0x03) != 0x03)
            return false;
        delayMicroseconds(12); // >=6 us required at the configured gyro ODR
    }
    // One checked burst covers timestamp, temperature and all six sensor axes.
    // Reading through GZ_H also releases a retained SyncSample data lock.
    unsigned char buf_reg[17];
    Wire.beginTransmission(QMI8658_slave_addr);
    Wire.write(QMI8658Register_Timestamp_L);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom(QMI8658_slave_addr, sizeof(buf_reg)) != sizeof(buf_reg)) {
        while (Wire.available())
            Wire.read();
        if (rawReadLocked) {
            unsigned char last;
            QMI8658_read_reg(QMI8658Register_Gz_H, &last, 1);
        }
        return false;
    }
    for (unsigned int i = 0; i < sizeof(buf_reg); ++i)
        buf_reg[i] = Wire.read();

    if (tim_count)
        *tim_count = (unsigned int)buf_reg[0] | ((unsigned int)buf_reg[1] << 8) |
                     ((unsigned int)buf_reg[2] << 16);
    for (unsigned int axis = 0; axis < 3; ++axis) {
        const unsigned int a = 5 + axis * 2, g = 11 + axis * 2;
        raw_acc_xyz[axis] =
            (short)((unsigned short)buf_reg[a] | ((unsigned short)buf_reg[a + 1] << 8));
        raw_gyro_xyz[axis] =
            (short)((unsigned short)buf_reg[g] | ((unsigned short)buf_reg[g + 1] << 8));
    }
    return true;
}

void QMI8658_config_acc(QMI8658_AccRange range, QMI8658_AccOdr odr) {
    QMI8658_write_reg(QMI8658Register_Ctrl2, uint8_t(range) | uint8_t(odr));
    // Start with filters off, matching the original driver. The sketch applies
    // its measured knock filter in tuneImuFilters() after normal initialization.
    QMI8658_write_reg(QMI8658Register_Ctrl5, 0);
}

void QMI8658_enableSensors(unsigned char flags) {
    QMI8658_write_reg(QMI8658Register_Ctrl7, flags & QMI8658_CTRL7_ENABLE_MASK);
}

unsigned char QMI8658_init(void) {
    for (uint8_t address = ADDRESS_LOW; address <= ADDRESS_HIGH; ++address) {
        QMI8658_slave_addr = address;
        for (int retry = 0; retry < 5; ++retry) {
            uint8_t id = 0;
            QMI8658_read_reg(QMI8658Register_WhoAmI, &id, 1);
            if (id != 5)
                continue;
            QMI8658_write_reg(QMI8658Register_Ctrl1, 0x60);
            QMI8658_config_acc(QMI8658AccRange_8g, QMI8658AccOdr_1000Hz);
            QMI8658_write_reg(QMI8658Register_Ctrl3,
                              QMI8658GyrRange_2048dps | QMI8658GyrOdr_1000Hz);
            QMI8658_write_reg(QMI8658Register_Ctrl5, 0);
            QMI8658_enableSensors(QMI8658_CTRL7_ACC_ENABLE | QMI8658_CTRL7_GYR_ENABLE);
            return 1;
        }
    }
    return 0;
}
