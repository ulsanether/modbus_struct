/**
Modbus slave implementation for STM32 HAL under FreeRTOS.
(c) 2017 Viacheslav Kaloshin, multik@multik.org
Licensed under LGPL. 
**/


#include <string.h>


#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "modbus.h"

#include "main.h"



extern ModbusData modbus_data;
extern UART_HandleTypeDef huart5;

// If you want directly send to usb-cdc
// #include "usbd_cdc_if.h"

osMessageQId ModBusInHandle;
osMessageQId ModBusOutHandle;
osThreadId ModBusTaskHandle;

uint16_t mb_reg[ModBusRegisters];

// Here is actual modbus data stores
uint8_t mb_buf_in[256];
uint8_t mb_buf_in_count;
uint8_t mb_addr;
uint8_t mb_buf_out[256];
uint8_t mb_buf_out_count;


void Lock_ModbusData(void);
void Unlock_ModbusData(void);
void Toggle_LED(uint8_t led_num);

void ModBusParse(void);
void ModBusTask(void const * argument)
{
    uint8_t  tx_buf[256];
    uint16_t tx_count = 0;

    for (;;)
    {
        // T3.5 (문자 간 침묵 시간) 타임아웃 대기
        osEvent evt = osMessageGet(ModBusInHandle, ModBus35);

        if (evt.status == osEventMessage)
        {
            uint8_t byte = (uint8_t)evt.value.v;
            if (mb_buf_in_count < sizeof(mb_buf_in))
            {
                mb_buf_in[mb_buf_in_count++] = byte;
            }
            else
            {
                mb_buf_in_count = 0; // 버퍼 오버플로우 방지
            }
        }
        else if (evt.status == osEventTimeout)
        {
            // 타임아웃 발생 & 데이터가 있다면 -> 프레임 수신 완료로 간주
            if (mb_buf_in_count > 0)
            {
                // [수정 1] 파싱 전에 센서/App 데이터를 레지스터로 가져옴 (Read 명령 대비)
                // App(Struct) -> Register
                Lock_ModbusData();
                ModBus_SyncDataToRegisters(&modbus_data);
                Unlock_ModbusData();

                // [수정 2] 파싱 실행 (여기서 마스터의 Read 요청에 응답하거나, Write 요청에 의해 mb_reg가 바뀜)
                ModBusParse();

                // [수정 3] 파싱 후에 변경된 레지스터 값을 App 데이터로 반영 (Write 명령 처리)
                // Register -> App(Struct)
                Lock_ModbusData();
                ModBus_SyncRegistersToData(&modbus_data);
                Unlock_ModbusData();

                // [수정 4] 응답 데이터 전송 로직 개선
                // 큐에서 모든 데이터를 꺼내서 버퍼에 담음
                tx_count = 0;
                while (1)
                {
                    osEvent evtOut = osMessageGet(ModBusOutHandle, 0); // 대기 시간 0 (즉시 리턴)
                    if (evtOut.status != osEventMessage)
                    {
                        break; // 더 이상 보낼 데이터가 없음
                    }

                    if (tx_count < sizeof(tx_buf))
                    {
                        tx_buf[tx_count++] = (uint8_t)evtOut.value.v;
                    }
                }

                // [수정 5] UART 전송 (DMA 대신 Blocking 사용 추천)
                // 이유: DMA는 전송 중 함수가 바로 리턴되므로, 다음 루프나 로직과 충돌할 수 있습니다.
                // 태스크 환경이므로 HAL_UART_Transmit을 써도 시스템 전체가 멈추지 않고 이 태스크만 대기합니다.
                if (tx_count > 0)
                {
                    // USB CDC인 경우
                    // CDC_Transmit_FS(tx_buf, tx_count);

                    // UART인 경우 (안정성을 위해 Blocking 모드 권장)
                    HAL_UART_Transmit(&huart5, tx_buf, tx_count, 100);

                    // 만약 꼭 DMA를 써야 한다면, 전송 완료 인터럽트에서 세마포어를 주고 여기서 기다려야 합니다.
                    // HAL_UART_Transmit_DMA(&huart5, tx_buf, tx_count);
                    // osSemaphoreWait(MyTxSemHandle, osWaitForever);

                    Toggle_LED(4); // TX LED
                }
            }
            // 처리 후 입력 버퍼 리셋
            mb_buf_in_count = 0;
        }
    }
}


void ModBus_Init(void)
{
  osMessageQDef(ModBusIn, 256, uint8_t);
  ModBusInHandle = osMessageCreate(osMessageQ(ModBusIn), NULL);
  osMessageQDef(ModBusOut, 256, uint8_t);
  ModBusOutHandle = osMessageCreate(osMessageQ(ModBusOut), NULL);
  osThreadDef(ModBusTask, ModBusTask, osPriorityNormal, 0, 128);
  ModBusTaskHandle = osThreadCreate(osThread(ModBusTask), NULL);
  mb_buf_in_count=0;
  mb_addr=247;
  mb_buf_out_count=0;
  for(int i=0;i<ModBusRegisters;i++) 
  {
    mb_reg[i]=0;
  }
}




void ModBus_SetAddress(uint8_t addr)
{
  mb_addr = addr;
}

void CRC16_OUT(void);
uint8_t CRC16_IN(void);
// parse something in incoming buffer 
void ModBusParse(void)
{
    if (mb_buf_in_count == 0) // call as by mistake on empty buffer?
    {
        return;
    }

    if (mb_buf_in[0] != mb_addr) // its not our address!
    {
        return;
    }

    // check CRC
    if (CRC16_IN() == 0)
    {
        mb_buf_out_count = 0;
        uint16_t st, nu;
        uint8_t func = mb_buf_in[1];
        uint8_t i;

        // mb_reg[] 접근 보호
        Lock_ModbusData();

        switch (func)
        {
            case 3:
                // read holding registers. by bytes addr func starth startl totalh totall
                st = mb_buf_in[2] * 256 + mb_buf_in[3];
                nu = mb_buf_in[4] * 256 + mb_buf_in[5];
                if ((st + nu) > ModBusRegisters) // dont ask more, that we has!
                {
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func + 0x80;
                    mb_buf_out[mb_buf_out_count++] = 2;
                }
                else
                {
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func;
                    mb_buf_out[mb_buf_out_count++] = nu * 2; // how many bytes we will send?
                    for (i = st; i < (st + nu); i++)
                    {
                        mb_buf_out[mb_buf_out_count++] = (mb_reg[i] >> 8) & 0xFF; // hi part
                        mb_buf_out[mb_buf_out_count++] = mb_reg[i] & 0xFF;        // lo part
                    }
                }
                break;

            case 5:
            {
                // Write Single Coil
                uint16_t coil_addr  = mb_buf_in[2] * 256 + mb_buf_in[3];
                uint16_t coil_value = mb_buf_in[4] * 256 + mb_buf_in[5];

                if (coil_addr >= ModBusRegisters)
                {
                    // 예외 응답: Illegal Data Address
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func + 0x80;
                    mb_buf_out[mb_buf_out_count++] = 2;
                }
                else
                {
                    // Coil 값 설정 (0xFF00 -> ON, 0x0000 -> OFF)
                    if (coil_value == 0xFF00)
                    {
                        mb_reg[coil_addr] = 1;  // ON
                    }
                    else if (coil_value == 0x0000)
                    {
                        mb_reg[coil_addr] = 0;  // OFF
                    }
                    else
                    {
                        // 잘못된 값이면 예외 응답
                        mb_buf_out[mb_buf_out_count++] = mb_addr;
                        mb_buf_out[mb_buf_out_count++] = func + 0x80;
                        mb_buf_out[mb_buf_out_count++] = 3; // Illegal data value
                        break;
                    }

                    // 정상 응답 (요청 프레임 그대로 반환)
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func;
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[2];
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[3];
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[4];
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[5];
                }
                break;
            }

            case 6:
            {
                // write single register. by bytes addr func starth startl valueh valuel
                uint16_t st;
                uint16_t write_value;

                st          = mb_buf_in[2] * 256 + mb_buf_in[3];
                write_value = mb_buf_in[4] * 256 + mb_buf_in[5];

                if (st >= ModBusRegisters)
                {
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func + 0x80;
                    mb_buf_out[mb_buf_out_count++] = 2;  // Illegal address exception
                }
                else
                {
                    // 값 저장
                    mb_reg[st] = write_value;

                    // 정상 응답 (요청과 동일한 형식)
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func;
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[2];  // register addr H
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[3];  // register addr L
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[4];  // value H
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[5];  // value L
                }
                break;
            }

            case 16:
                // write holding registers. by bytes addr func starth startl totalh totall num_bytes regh regl ...
                st = mb_buf_in[2] * 256 + mb_buf_in[3];
                nu = mb_buf_in[4] * 256 + mb_buf_in[5];
                if ((st + nu) > ModBusRegisters) // dont ask more, that we has!
                {
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func + 0x80;
                    mb_buf_out[mb_buf_out_count++] = 2;
                }
                else
                { // ATTN : skip num_bytes
                    for (i = 0; i < nu; i++)
                    {
                        mb_reg[st + i] = mb_buf_in[7 + i * 2] * 256 + mb_buf_in[8 + i * 2];
                    }
                    mb_buf_out[mb_buf_out_count++] = mb_addr;
                    mb_buf_out[mb_buf_out_count++] = func;
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[2]; // how many registers ask, so many wrote
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[3];
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[4];
                    mb_buf_out[mb_buf_out_count++] = mb_buf_in[5];
                }
                break;

            default:
                // Exception as we does not provide this function
                mb_buf_out[mb_buf_out_count++] = mb_addr;
                mb_buf_out[mb_buf_out_count++] = func + 0x80;
                mb_buf_out[mb_buf_out_count++] = 1;
                break;
        }

        Unlock_ModbusData();

        CRC16_OUT();

        // If you want directly to USB-CDC
        //CDC_Transmit_FS(&mb_buf_out[0], mb_buf_out_count);
        for (int i = 0; i < mb_buf_out_count; i++)
        {
            osMessagePut(ModBusOutHandle, mb_buf_out[i], 0);
        }
    }

    // Ok, we parsed buffer, clean up
    mb_buf_in_count  = 0;
    mb_buf_out_count = 0;
}

// set value of register
void ModBus_SetRegister(uint8_t reg,uint16_t value)
{
  if(reg<ModBusRegisters)
  {
    mb_reg[reg]=value;
  }
}
// grab value of register
uint16_t ModBus_GetRegister(uint8_t reg)
{
  if(reg<ModBusRegisters)
  {
    return mb_reg[reg];
  }
  return 0;
}


// Calculate CRC for outcoming buffer
// and place it to end.
void CRC16_OUT(void)
{
  uint16_t crc = 0xFFFF;
  uint16_t pos = 0;
  uint8_t i =0;
  uint8_t lo =0;
  uint8_t hi =0;
  
  for (pos = 0; pos < mb_buf_out_count; pos++)
  {
    crc ^= mb_buf_out[pos];

  for (i = 8; i != 0; i--)
    {
    if ((crc & 0x0001) != 0)
      {
      crc >>= 1;
      crc ^= 0xA001;
      }
    else
      crc >>= 1;
    }
  }
  lo = crc & 0xFF;
  hi = ( crc >> 8 ) & 0xFF;
  
  mb_buf_out[mb_buf_out_count++] = lo;
  mb_buf_out[mb_buf_out_count++] = hi;
}

// Calculate CRC fro incoming buffer
// Return 0 - if CRC is correct, overwise return 0 
uint8_t CRC16_IN(void)
{
  uint16_t crc = 0xFFFF;
  uint16_t pos = 0;
  uint8_t i =0;
  uint8_t lo =0;
  uint8_t hi =0;
  
  for (pos = 0; pos < mb_buf_in_count-2; pos++)
  {
    crc ^= mb_buf_in[pos];

  for (i = 8; i != 0; i--)
    {
    if ((crc & 0x0001) != 0)
      {
      crc >>= 1;
      crc ^= 0xA001;
      }
    else
      crc >>= 1;
    }
  }
  lo = crc & 0xFF;
  hi = ( crc >> 8 ) & 0xFF;
  if( (mb_buf_in[mb_buf_in_count-2] == lo) && 
       (mb_buf_in[mb_buf_in_count-1] == hi) )
    {
      return 0;
    }
  return 1;
}



void ModBus_SyncRegistersToData(ModbusData *data)
{
    uint16_t *dst = (uint16_t *)data;

    for (uint16_t i = 0; i < MODBUS_DATA_WORD_COUNT; i++)
    {
        dst[i] = ModBus_GetRegister(MODBUS_DATA_BASE_ADDR + i);
    }
}


void ModBus_SyncDataToRegisters(ModbusData *data)
{
    uint16_t *src = (uint16_t *)data;

    for (uint16_t i = 0; i < MODBUS_DATA_WORD_COUNT; i++)
    {
        if ((MODBUS_DATA_BASE_ADDR + i) < ModBusRegisters) {
             ModBus_SetRegister(MODBUS_DATA_BASE_ADDR + i, src[i]);
        }
    }
}
