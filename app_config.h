#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H



//앱버전
#define APP_VERSION 90 //90 *100  =  0.9



//시스템 클럭 주파수
#define SYSTEM_CLOCK_PREQ 100000000 //100MHz
#define SYSTEM_CLOCK_KHZ 100000 //100MHz
#define SYSTEM_CLOCK_MHZ 100 //100MHz


//ADC 설정
#define ADC_RESOLUTION_BIT 12 //12bit
#define ADC_MAX_VALUE 4095 //2^12 -1


//ADC 매핑
#define ADC_CHANNEL_PLC_AUX ADC_CHANNEL_1
#define ADC_CHANNEL_EXT_AIN ADC_CHANNEL_2
#define ADC_CHANNEL_SOLA_CURRENT ADC_CHANNEL_3
#define ADC_CHANNEL_SOLB_CURRENT ADC_CHANNEL_4






//모드버스 데이터 구조체


/* Solenoid data */
typedef struct __attribute__((packed)) {
    uint16_t sola_setpoint;   //sola 설정값
    uint16_t sola_measurement;  //sola 측정값
    uint16_t solb_setpoint; //solb 설정값
    uint16_t solb_measurement; //solb 측정값
    uint16_t plc_aux;        //PLC 보조 입력
    uint16_t ext_ain;		//외부 아날로그 입력
    uint16_t sola_current; //sola 전류값
    uint16_t solb_current; //solb 전류값
    uint8_t  dip_switch;  //딥스위치 상태
    uint8_t  status_sys; //시스템 상태
    uint8_t  errorCode; //에러 코드
} SolenoidData;

/* PI controller */
typedef struct __attribute__((packed)) {
    int16_t sola_Kp;
    int16_t sola_Ki;
    int16_t solb_Kp;
    int16_t solb_Ki;
    int16_t integral;
    int16_t prev_error;
    int16_t out_min;
    int16_t out_max;
    int16_t integral_min;
    int16_t integral_max;
} PI_Controller;

/* EEPROM stored parameters */
typedef struct __attribute__((packed)) {
    uint16_t sola_Kp;
    uint16_t sola_Ki;
    uint16_t solb_Kp;
    uint16_t solb_Ki;
    uint16_t reserved[4];
} EEPROM_Data;

/* Modbus mapped data */
typedef struct __attribute__((packed)) {
    uint16_t     version;
    uint16_t     reserved[4];
    SolenoidData solenoid_data;
    uint16_t     reserved2[2];
    PI_Controller pi_controller;
    uint16_t     reserved3[2];
    EEPROM_Data  eeprom_data;
} ModbusData;







#endif /* __APP_CONFIG_H */
