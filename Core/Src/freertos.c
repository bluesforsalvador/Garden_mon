#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
#include "cli.h"

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
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for cliTask */
osThreadId_t cliTaskHandle;
const osThreadAttr_t cliTask_attributes = {
  .name = "cliTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
void StartDefaultTask(void *argument);

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN FunctionPrototypes */
/* USER CODE END FunctionPrototypes */

void MX_FREERTOS_Init(void) {
  /* Init code for FreeRTOS */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  cliTaskHandle = osThreadNew(CLI_Task, NULL, &cliTask_attributes);
}

/* USER CODE BEGIN Header_StartDefaultTask */
void StartDefaultTask(void *argument) {
  for(;;) {
    osDelay(1);
  }
}
/* USER CODE END Header_StartDefaultTask */
