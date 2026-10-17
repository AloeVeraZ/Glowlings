#ifndef QMI8658_H
#define QMI8658_H

#include "DEV_Config.h"
#include <stdint.h>
#define QMI8658_CTRL7_DISABLE_ALL (0x0)
#define QMI8658_CTRL7_ACC_ENABLE (0x1)
#define QMI8658_CTRL7_GYR_ENABLE (0x2)
#define QMI8658_CTRL7_MAG_ENABLE (0x4)
#define QMI8658_CTRL7_AE_ENABLE (0x8)
#define QMI8658_CTRL7_GYR_SNOOZE_ENABLE (0x10)
#define QMI8658_CTRL7_ENABLE_MASK (0xF)


#define QMI8658_STATUS1_CMD_DONE (0x01)
#define QMI8658_STATUS1_WAKEUP_EVENT (0x04)

enum QMI8658Register
{
    /*! \brief FIS device identifier register. */
    QMI8658Register_WhoAmI = 0, // 0
    /*! \brief FIS hardware revision register. */
    QMI8658Register_Revision, // 1
    /*! \brief General and power management modes. */
    QMI8658Register_Ctrl1, // 2
    /*! \brief Accelerometer control. */
    QMI8658Register_Ctrl2, // 3
    /*! \brief Gyroscope control. */
    QMI8658Register_Ctrl3, // 4
    /*! \brief Magnetometer control. */
    QMI8658Register_Ctrl4, // 5
    /*! \brief Data processing settings. */
    QMI8658Register_Ctrl5, // 6
    /*! \brief AttitudeEngine control. */
    QMI8658Register_Ctrl6, // 7
    /*! \brief Sensor enabled status. */
    QMI8658Register_Ctrl7, // 8
    /*! \brief Reserved - do not write. */
    QMI8658Register_Ctrl8, // 9
    /*! \brief Host command register. */
    QMI8658Register_Ctrl9, // 10
    /*! \brief Calibration register 1 most significant byte. */
    QMI8658Register_Cal1_L = 11,
    /*! \brief Calibration register 1 least significant byte. */
    QMI8658Register_Cal1_H,
    /*! \brief Calibration register 2 most significant byte. */
    QMI8658Register_Cal2_L,
    /*! \brief Calibration register 2 least significant byte. */
    QMI8658Register_Cal2_H,
    /*! \brief Calibration register 3 most significant byte. */
    QMI8658Register_Cal3_L,
    /*! \brief Calibration register 3 least significant byte. */
    QMI8658Register_Cal3_H,
    /*! \brief Calibration register 4 most significant byte. */
    QMI8658Register_Cal4_L,
    /*! \brief Calibration register 4 least significant byte. */
    QMI8658Register_Cal4_H,
    /*! \brief FIFO control register. */
    QMI8658Register_FifoCtrl = 19,
    /*! \brief FIFO data register. */
    QMI8658Register_FifoData, // 20
    /*! \brief FIFO status register. */
    QMI8658Register_FifoStatus, // 21
    /*! \brief Output data overrun and availability. */
    QMI8658Register_StatusInt = 45,
    /*! \brief Output data overrun and availability. */
    QMI8658Register_Status0,
    /*! \brief Miscellaneous status register. */
    QMI8658Register_Status1,
    /*! \brief timestamp low. */
    QMI8658Register_Timestamp_L = 48,
    /*! \brief timestamp low. */
    QMI8658Register_Timestamp_M,
    /*! \brief timestamp low. */
    QMI8658Register_Timestamp_H,
    /*! \brief tempearture low. */
    QMI8658Register_Tempearture_L = 51,
    /*! \brief tempearture low. */
    QMI8658Register_Tempearture_H,
    /*! \brief Accelerometer X axis least significant byte. */
    QMI8658Register_Ax_L = 53,
    /*! \brief Accelerometer X axis most significant byte. */
    QMI8658Register_Ax_H,
    /*! \brief Accelerometer Y axis least significant byte. */
    QMI8658Register_Ay_L,
    /*! \brief Accelerometer Y axis most significant byte. */
    QMI8658Register_Ay_H,
    /*! \brief Accelerometer Z axis least significant byte. */
    QMI8658Register_Az_L,
    /*! \brief Accelerometer Z axis most significant byte. */
    QMI8658Register_Az_H,
    /*! \brief Gyroscope X axis least significant byte. */
    QMI8658Register_Gx_L = 59,
    /*! \brief Gyroscope X axis most significant byte. */
    QMI8658Register_Gx_H,
    /*! \brief Gyroscope Y axis least significant byte. */
    QMI8658Register_Gy_L,
    /*! \brief Gyroscope Y axis most significant byte. */
    QMI8658Register_Gy_H,
    /*! \brief Gyroscope Z axis least significant byte. */
    QMI8658Register_Gz_L,
    /*! \brief Gyroscope Z axis most significant byte. */
    QMI8658Register_Gz_H,
    /*! \brief Magnetometer X axis least significant byte. */
    QMI8658Register_Mx_L = 65,
    /*! \brief Magnetometer X axis most significant byte. */
    QMI8658Register_Mx_H,
    /*! \brief Magnetometer Y axis least significant byte. */
    QMI8658Register_My_L,
    /*! \brief Magnetometer Y axis most significant byte. */
    QMI8658Register_My_H,
    /*! \brief Magnetometer Z axis least significant byte. */
    QMI8658Register_Mz_L,
    /*! \brief Magnetometer Z axis most significant byte. */
    QMI8658Register_Mz_H,
    /*! \brief Quaternion increment W least significant byte. */
    QMI8658Register_Q1_L = 73,
    /*! \brief Quaternion increment W most significant byte. */
    QMI8658Register_Q1_H,
    /*! \brief Quaternion increment X least significant byte. */
    QMI8658Register_Q2_L,
    /*! \brief Quaternion increment X most significant byte. */
    QMI8658Register_Q2_H,
    /*! \brief Quaternion increment Y least significant byte. */
    QMI8658Register_Q3_L,
    /*! \brief Quaternion increment Y most significant byte. */
    QMI8658Register_Q3_H,
    /*! \brief Quaternion increment Z least significant byte. */
    QMI8658Register_Q4_L,
    /*! \brief Quaternion increment Z most significant byte. */
    QMI8658Register_Q4_H,
    /*! \brief Velocity increment X least significant byte. */
    QMI8658Register_Dvx_L = 81,
    /*! \brief Velocity increment X most significant byte. */
    QMI8658Register_Dvx_H,
    /*! \brief Velocity increment Y least significant byte. */
    QMI8658Register_Dvy_L,
    /*! \brief Velocity increment Y most significant byte. */
    QMI8658Register_Dvy_H,
    /*! \brief Velocity increment Z least significant byte. */
    QMI8658Register_Dvz_L,
    /*! \brief Velocity increment Z most significant byte. */
    QMI8658Register_Dvz_H,
    /*! \brief AttitudeEngine reg1. */
    QMI8658Register_AeReg1 = 87,
    /*! \brief AttitudeEngine overflow flags. */
    QMI8658Register_AeOverflow,

    QMI8658Register_I2CM_STATUS = 110
};

enum QMI8658_Ois_Register
{
    /*-----------------------------*/
    /* Setup and Control Registers */
    /*-----------------------------*/
    /*! \brief SPI Endian Selection, and SPI 3/4 Wire */
    QMI8658_OIS_Reg_Ctrl1 = 0x02, // 2  [0x02] -- Dflt: 0x20
    /*! \brief Accelerometer control: ODR, Full Scale, Self Test  */
    QMI8658_OIS_Reg_Ctrl2, //  3  [0x03]
    /*! \brief Gyroscope control: ODR, Full Scale, Self Test */
    QMI8658_OIS_Reg_Ctrl3, //  4  [0x04]
    /*! \brief Sensor Data Processing Settings */
    QMI8658_OIS_Reg_Ctrl5 = 0x06, //  6  [0x06]
    /*! \brief Sensor enabled status: Enable Sensors  */
    QMI8658_OIS_Reg_Ctrl7 = 0x08, //  8  [0x08]
    /*-------------------*/
    /* Status Registers  */
    /*-------------------*/
    /*! \brief Sensor Data Availability and Lock Register */
    QMI8658_OIS_Reg_StatusInt = 0x2D, // 45  [0x2D]
    /*! \brief Output data overrun and availability */
    QMI8658_OIS_Reg_Status0 = 0x2E, // 46  [0x2E]

    /*-----------------------------------------------------*/
    /* OIS Sensor Data Output Registers. 16-bit 2's complement */
    /*-----------------------------------------------------*/
    /*! \brief Accelerometer X axis least significant byte */
    QMI8658_OIS_Reg_Ax_L = 0x33, // 53  [0x35]
    /*! \brief Accelerometer X axis most significant byte */
    QMI8658_OIS_Reg_Ax_H, // 54  [0x36]
    /*! \brief Accelerometer Y axis least significant byte */
    QMI8658_OIS_Reg_Ay_L, // 55  [0x37]
    /*! \brief Accelerometer Y axis most significant byte */
    QMI8658_OIS_Reg_Ay_H, // 56  [0x38]
    /*! \brief Accelerometer Z axis least significant byte */
    QMI8658_OIS_Reg_Az_L, // 57  [0x39]
    /*! \brief Accelerometer Z axis most significant byte */
    QMI8658_OIS_Reg_Az_H, // 58  [0x3A]

    /*! \brief Gyroscope X axis least significant byte */
    QMI8658_OIS_Reg_Gx_L = 0x3B, // 59  [0x3B]
    /*! \brief Gyroscope X axis most significant byte */
    QMI8658_OIS_Reg_Gx_H, // 60  [0x3C]
    /*! \brief Gyroscope Y axis least significant byte */
    QMI8658_OIS_Reg_Gy_L, // 61  [0x3D]
    /*! \brief Gyroscope Y axis most significant byte */
    QMI8658_OIS_Reg_Gy_H, // 62  [0x3E]
    /*! \brief Gyroscope Z axis least significant byte */
    QMI8658_OIS_Reg_Gz_L, // 63  [0x3F]
    /*! \brief Gyroscope Z axis most significant byte */
    QMI8658_OIS_Reg_Gz_H, // 64  [0x40]
};

enum QMI8658_Ctrl9Command
{
    QMI8658_Ctrl9_Cmd_NOP = 0X00,
    QMI8658_Ctrl9_Cmd_GyroBias = 0X01,
    QMI8658_Ctrl9_Cmd_Rqst_Sdi_Mod = 0X03,
    QMI8658_Ctrl9_Cmd_WoM_Setting = 0x08,
    QMI8658_Ctrl9_Cmd_AccelHostDeltaOffset = 0x09,
    QMI8658_Ctrl9_Cmd_GyroHostDeltaOffset = 0x0A,
    QMI8658_Ctrl9_Cmd_Dbg_WoM_Data_Enable = 0xF8,

};

enum QMI8658_LpfMode
{
    A_LSP_MODE_0 = 0x00 << 1,
    A_LSP_MODE_1 = 0x01 << 1,
    A_LSP_MODE_2 = 0x02 << 1,
    A_LSP_MODE_3 = 0x03 << 1,

    G_LSP_MODE_0 = 0x00 << 5,
    G_LSP_MODE_1 = 0x01 << 5,
    G_LSP_MODE_2 = 0x02 << 5,
    G_LSP_MODE_3 = 0x03 << 5
};

enum QMI8658_AccRange
{
    QMI8658AccRange_2g = 0x00 << 4, /*!< \brief +/- 2g range */
    QMI8658AccRange_4g = 0x01 << 4, /*!< \brief +/- 4g range */
    QMI8658AccRange_8g = 0x02 << 4, /*!< \brief +/- 8g range */
    QMI8658AccRange_16g = 0x03 << 4 /*!< \brief +/- 16g range */
};

enum QMI8658_AccOdr
{
    QMI8658AccOdr_8000Hz = 0x00,         /*!< \brief High resolution 8000Hz output rate. */
    QMI8658AccOdr_4000Hz = 0x01,         /*!< \brief High resolution 4000Hz output rate. */
    QMI8658AccOdr_2000Hz = 0x02,         /*!< \brief High resolution 2000Hz output rate. */
    QMI8658AccOdr_1000Hz = 0x03,         /*!< \brief High resolution 1000Hz output rate. */
    QMI8658AccOdr_500Hz = 0x04,          /*!< \brief High resolution 500Hz output rate. */
    QMI8658AccOdr_250Hz = 0x05,          /*!< \brief High resolution 250Hz output rate. */
    QMI8658AccOdr_125Hz = 0x06,          /*!< \brief High resolution 125Hz output rate. */
    QMI8658AccOdr_62_5Hz = 0x07,         /*!< \brief High resolution 62.5Hz output rate. */
    QMI8658AccOdr_31_25Hz = 0x08,        /*!< \brief High resolution 31.25Hz output rate. */
    QMI8658AccOdr_LowPower_128Hz = 0x0c, /*!< \brief Low power 128Hz output rate. */
    QMI8658AccOdr_LowPower_21Hz = 0x0d,  /*!< \brief Low power 21Hz output rate. */
    QMI8658AccOdr_LowPower_11Hz = 0x0e,  /*!< \brief Low power 11Hz output rate. */
    QMI8658AccOdr_LowPower_3Hz = 0x0f    /*!< \brief Low power 3Hz output rate. */
};

enum QMI8658_GyrRange
{
    QMI8658GyrRange_32dps = 0 << 4,   /*!< \brief +-32 degrees per second. */
    QMI8658GyrRange_64dps = 1 << 4,   /*!< \brief +-64 degrees per second. */
    QMI8658GyrRange_128dps = 2 << 4,  /*!< \brief +-128 degrees per second. */
    QMI8658GyrRange_256dps = 3 << 4,  /*!< \brief +-256 degrees per second. */
    QMI8658GyrRange_512dps = 4 << 4,  /*!< \brief +-512 degrees per second. */
    QMI8658GyrRange_1024dps = 5 << 4, /*!< \brief +-1024 degrees per second. */
    QMI8658GyrRange_2048dps = 6 << 4, /*!< \brief +-2048 degrees per second. */
    QMI8658GyrRange_4096dps = 7 << 4  /*!< \brief +-2560 degrees per second. */
};

/*!
 * \brief Gyroscope output rate configuration.
 */
enum QMI8658_GyrOdr
{
    QMI8658GyrOdr_8000Hz = 0x00, /*!< \brief High resolution 8000Hz output rate. */
    QMI8658GyrOdr_4000Hz = 0x01, /*!< \brief High resolution 4000Hz output rate. */
    QMI8658GyrOdr_2000Hz = 0x02, /*!< \brief High resolution 2000Hz output rate. */
    QMI8658GyrOdr_1000Hz = 0x03, /*!< \brief High resolution 1000Hz output rate. */
    QMI8658GyrOdr_500Hz = 0x04,  /*!< \brief High resolution 500Hz output rate. */
    QMI8658GyrOdr_250Hz = 0x05,  /*!< \brief High resolution 250Hz output rate. */
    QMI8658GyrOdr_125Hz = 0x06,  /*!< \brief High resolution 125Hz output rate. */
    QMI8658GyrOdr_62_5Hz = 0x07, /*!< \brief High resolution 62.5Hz output rate. */
    QMI8658GyrOdr_31_25Hz = 0x08 /*!< \brief High resolution 31.25Hz output rate. */
};

extern unsigned char QMI8658_write_reg(unsigned char reg, unsigned char value);
extern unsigned char QMI8658_read_reg(unsigned char reg, unsigned char *buf, unsigned short len);
extern unsigned char QMI8658_init(void);
extern void QMI8658_config_acc(QMI8658_AccRange range,QMI8658_AccOdr odr);
extern void QMI8658_enableSensors(unsigned char enableFlags);
// Returns false on a failed/incomplete I2C read. tim_count is the native
// 24-bit sample counter, which wraps at 0xFFFFFF.
extern bool QMI8658_read_xyz_raw(short raw_acc_xyz[3], short raw_gyro_xyz[3], unsigned int *tim_count);
extern void QMI8658_setRawReadLock(bool enabled);
extern float QMI8658_readTemp(void);

#endif
