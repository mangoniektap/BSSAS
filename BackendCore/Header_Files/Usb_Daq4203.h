/**
 * @file Usb_Daq4203.h
 * @brief USB DAQ4203 数据采集卡 DLL 函数声明，提供 AD/DA、PWM、数字 IO 等硬件接口。
 */

#ifndef USB_DAQ4203_H
#define USB_DAQ4203_H

// 如果使用DLL导出则定义USB_DAQ_V52_DLL_EXPORTS
// 如果使用DLL导入则不需要定义
#ifdef USB_DAQ_V52_DLL_EXPORTS
#define USB_DAQ_V52_DLL_API extern "C" __declspec(dllexport)
#else
#define USB_DAQ_V52_DLL_API extern "C" __declspec(dllimport)
#endif

/** @brief 打开 USB 设备 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall openUSB(void);
/** @brief 关闭 USB 设备 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall closeUSB(void);

/** @brief 获取已连接的设备数量 @returns 设备数量 */
USB_DAQ_V52_DLL_API int __stdcall get_device_num(void);
/** @brief 复位指定 USB 设备 @param dev 子设备号 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Reset_Usb_Device(int dev);
/**
 * @brief 单次 AD 采集
 * @param dev     子设备号
 * @param ad_os   AD 过采样率
 * @param ad_range AD 量程: 0=±5V(0.68mV), 1=±10V(1.22mV)
 * @param databuf 采集数据缓冲区
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall ad_single(int dev, int ad_os, int ad_range, float* databuf);
/**
 * @brief 配置连续 AD 采集
 * @param dev        子设备号，第一个插上电脑的为0，以此类推
 * @param ad_os      设定AD采集的过采样率
 * @param ad_range   设置AD量程: 0=±5V(0.68mV), 1=±10V(1.22mV)
 * @param freq       连续采样频率 (100-200000 Hz)
 * @param trig_sl    设置触发模式: 0=软件启动一次采样过程, 1=外部触发启动一次采样过程
 * @param trig_pol   触发极性
 * @param clk_sl     时钟源选择
 * @param ext_clk_pol 外部时钟极性
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall ad_continu_conf(int dev, int ad_os, int ad_range, int freq, int trig_sl, int trig_pol, int clk_sl, int ext_clk_pol);
/** @brief 获取 AD 缓冲区大小 @param dev 子设备号 @returns 缓冲区大小 */
USB_DAQ_V52_DLL_API int __stdcall Get_AdBuf_Size(int dev);
/**
 * @brief 读取 AD 缓冲区数据
 * @param dev     子设备号
 * @param databuf 数据缓冲区
 * @param num     读取点数
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall Read_AdBuf(int dev, float* databuf, int num);
/** @brief 停止连续 AD 采集 @param dev 子设备号 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall AD_continu_stop(int dev);
/**
 * @brief 执行连续 AD 采集（含读取）
 * @param dev        子设备号
 * @param ad_os      AD 过采样率
 * @param ad_range   AD 量程
 * @param freq       连续采样频率
 * @param trig_sl    触发模式
 * @param trig_pol   触发极性
 * @param clk_sl     时钟源
 * @param ext_clk_pol 外部时钟极性
 * @param num        读取点数
 * @param databuf    数据缓冲区
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall ad_continu(int dev, int ad_os, int ad_range, int freq, int trig_sl, int trig_pol, int clk_sl, int ext_clk_pol, int num, float* databuf);
/**
 * @brief PWM 输出
 * @param dev  子设备号
 * @param ch   通道号 (0~3)
 * @param en   使能
 * @param freq 频率
 * @param duty 占空比 (0.0~1.0)
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall Pwm_Out(int dev, int ch, int en, int freq, float duty);
/**
 * @brief 脉冲输出
 * @param dev   子设备号
 * @param ch    通道号
 * @param pulse 脉冲数
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall Pulse_Out(int dev, int ch, int pulse);
/**
 * @brief 设置 PWM 输入
 * @param dev 子设备号
 * @param ch  通道号
 * @param en  使能
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall Set_Pwm_In(int dev, int ch, int en);
/**
 * @brief 读取 PWM 输入
 * @param dev  子设备号
 * @param ch   通道号
 * @param freq 输出频率 (Hz)
 * @param duty 输出占空比
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall Read_Pwm_In(int dev, int ch, float* freq, float* duty);

/** @brief 读取数字输入端口 @param dev 子设备号 @param in_port 输出端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Read_Port_In(int dev, unsigned short* in_port);
/** @brief 读取数字输出端口 @param dev 子设备号 @param out_port 输出端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Read_Port_Out(int dev, unsigned short* out_port);

/** @brief 写数字输出端口 @param dev 子设备号 @param out_port 端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Write_Port_Out(int dev, unsigned short out_port);
/** @brief 置位数字输出端口 @param dev 子设备号 @param out_port 端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Set_Port_Out(int dev, unsigned short out_port);
/** @brief 复位数字输出端口 @param dev 子设备号 @param out_port 端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Reset_Port_Out(int dev, unsigned short out_port);
/** @brief 写数字输出端口低字节 @param dev 子设备号 @param out_port 端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Write_Port_OutL(int dev, unsigned char out_port);
/** @brief 写数字输出端口高字节 @param dev 子设备号 @param out_port 端口值 @returns 操作状态码 */
USB_DAQ_V52_DLL_API int __stdcall Write_Port_OutH(int dev, unsigned char out_port);

/**
 * @brief 设置 DA 单次输出
 * @param dev      子设备号
 * @param ch       通道号
 * @param da_value DA 输出电压值
 * @returns 操作状态码
 */
USB_DAQ_V52_DLL_API int __stdcall Set_DA_Single(int dev, int ch, float da_value);

#endif // USB_DAQ4203_H