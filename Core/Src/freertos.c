/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "cli.h"
#include "adc.h"
#include "st7032.h"
#include "log_flash.h"
#include "ads1115.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
extern void CLI_Task(void *argument);
extern UART_HandleTypeDef huart6;
void MoistureDisplayTask(void *argument);  // forward declaration
void MoistureLogTask(void *argument);  // forward declaration for logging task

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  st7032_init_bar_chars();

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  /* USER CODE BEGIN RTOS_THREADS */
  xTaskCreate(MoistureDisplayTask, "Moisture", 256, NULL, 1, NULL);
  xTaskCreate(MoistureLogTask, "LogTask", 512, NULL, 1, NULL);

extern void CLI_Task(void *argument);

osThreadId_t cliTaskHandle;
const osThreadAttr_t cliTask_attributes = {
  .name = "cliTask",
  .stack_size = 1024 * 4,  // try increasing if CLI fails to run
  .priority = (osPriority_t) osPriorityNormal
};

cliTaskHandle = osThreadNew(CLI_Task, NULL, &cliTask_attributes);
if (cliTaskHandle == NULL) {
    HAL_UART_Transmit(&huart6, (uint8_t*)"CLI task creation FAILED\r\n", 27, HAL_MAX_DELAY);
} else {
    HAL_UART_Transmit(&huart6, (uint8_t*)"CLI task created OK\r\n", 22, HAL_MAX_DELAY);
}

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
void StartDefaultTask(void *argument) {
  for(;;) {
    osDelay(1);
  }
}
/* USER CODE END Header_StartDefaultTask */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void MoistureDisplayTask(void *argument) {
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t val_m1 = 0, val_m2 = 0;
    char line[17];

    for (;;) {
        // M1 = PA0
        sConfig.Channel = ADC_CHANNEL_0;
        sConfig.Rank = 1;
        sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        val_m1 = HAL_ADC_GetValue(&hadc1);

        // M2 = PA1
        sConfig.Channel = ADC_CHANNEL_1;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        val_m2 = HAL_ADC_GetValue(&hadc1);

        // Convert to percentage (wet = 100%, dry = 0%)
        uint8_t m1_pct = (val_m1 >= m1_cal.wet) ? 100 :
                         (val_m1 <= m1_cal.dry) ? 0 :
                         100 * (val_m1 - m1_cal.dry) / (m1_cal.wet - m1_cal.dry);

        uint8_t m2_pct = (val_m2 >= m2_cal.wet) ? 100 :
                         (val_m2 <= m2_cal.dry) ? 0 :
                         100 * (val_m2 - m2_cal.dry) / (m2_cal.wet - m2_cal.dry);



        // Update LCD
        st7032_draw_moisture_bar(0, m1_pct); // Line 0
        st7032_draw_moisture_bar(1, m2_pct); // Line 1

        vTaskDelay(pdMS_TO_TICKS(2000));  // Update every 2s
    }
}

void MoistureLogTask(void *argument) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        log_entry_t entry;

        // --- Read M1 (PA0)
        ADC_ChannelConfTypeDef sConfig = {0};
        sConfig.Channel = ADC_CHANNEL_0;
        sConfig.Rank = 1;
        sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        entry.m1 = HAL_ADC_GetValue(&hadc1);

        // --- Read M2 (PA1)
        sConfig.Channel = ADC_CHANNEL_1;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        entry.m2 = HAL_ADC_GetValue(&hadc1);

        // --- Read ADS channels
        for (uint8_t ch = 0; ch < 4; ch++) {
            entry.ads[ch] = ads_read_channel(ch);  // must be defined in cli.c or elsewhere
        }

        entry.timestamp_ms = xTaskGetTickCount();

        flash_write_log_entry(&entry);

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(60000)); // log every 60 seconds
    }
}


/* USER CODE END Application */

