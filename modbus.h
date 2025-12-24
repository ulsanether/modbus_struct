/**
Modbus slave implementation for STM32 HAL under FreeRTOS.
(c) 2017 Viacheslav Kaloshin, multik@multik.org
Licensed under LGPL. 
**/
#ifndef __modbus_H
#define __modbus_H
#ifdef __cplusplus
extern "C" {
#endif
#include "cmsis_os.h"
#include "app_config.h"  // ModbusData 구조체 사용

// 3,5 character timeout or frame timeout
// This value for 9600 speed and 1000Hz tick counter
/*

보드 레이트: 9600 bps
1개 문자 = 11 비트 (1 스타트 + 8 데이터 + 1 패리티 + 1 스톱)
1개 문자 시간 = 11비트 / 9600 bps ≈ 1.146 ms
3.5 문자 시간 ≈ 4 ms
#define ModBus35 35 그럴 경우 35 쓰면 됨. 

19200 - 18, 115200 - 2   9600 - 35 값
*/

#define MODBUS_DATA_BASE_ADDR   0    // 0번 레지스터부터 사용
#define MODBUS_DATA_WORD_COUNT  (sizeof(ModbusData) / 2)


#define ModBus35 35
// How many holding registers we are serve?
#define ModBusRegisters (sizeof(ModbusData) / sizeof(uint16_t))   //모드버스 데이터 크기 만큼 동적 할당 
//#define ModBusRegisters 10 // 0-9   <---------레지스터 크기  계산해서 넉넉하게 넣어야함
   // message queue for outgoing bytes
   extern osMessageQId ModBusInHandle;
   // message queue for outgoing bytes
   extern osMessageQId ModBusOutHandle;
   // just start it before scheduler
   void ModBus_Init(void);
   // set our address
   void ModBus_SetAddress(uint8_t addr);
   // set value of register
   void ModBus_SetRegister(uint8_t reg,uint16_t value);
   // grab value of register
   uint16_t ModBus_GetRegister(uint8_t reg);


   //struct를 던지는 구조로 할 경우 
   void ModBus_SyncDataToRegisters(ModbusData *data);
   void ModBus_SyncRegistersToData(ModbusData *data);

#ifdef __cplusplus
}
#endif
#endif
