/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SYS_LED1_Pin GPIO_PIN_13
#define SYS_LED1_GPIO_Port GPIOC
#define SYS_LED2_Pin GPIO_PIN_14
#define SYS_LED2_GPIO_Port GPIOC
#define ACK_LT_Pin GPIO_PIN_0
#define ACK_LT_GPIO_Port GPIOH
#define SW_COMMON_Pin GPIO_PIN_1
#define SW_COMMON_GPIO_Port GPIOH
#define SR_LATCH_Pin GPIO_PIN_4
#define SR_LATCH_GPIO_Port GPIOA
#define SR_OE_Pin GPIO_PIN_6
#define SR_OE_GPIO_Port GPIOA
#define SEL_P1_Pin GPIO_PIN_10
#define SEL_P1_GPIO_Port GPIOA
#define SEL_P2_Pin GPIO_PIN_11
#define SEL_P2_GPIO_Port GPIOA
#define I1_Pin GPIO_PIN_11
#define I1_GPIO_Port GPIOC
#define I2_Pin GPIO_PIN_12
#define I2_GPIO_Port GPIOC
#define I3_Pin GPIO_PIN_2
#define I3_GPIO_Port GPIOD
#define I4_Pin GPIO_PIN_3
#define I4_GPIO_Port GPIOB
#define I5_Pin GPIO_PIN_4
#define I5_GPIO_Port GPIOB
#define I6_Pin GPIO_PIN_5
#define I6_GPIO_Port GPIOB
#define I7_Pin GPIO_PIN_6
#define I7_GPIO_Port GPIOB
#define I8_Pin GPIO_PIN_7
#define I8_GPIO_Port GPIOB
#define AC1_IN_Pin GPIO_PIN_8
#define AC1_IN_GPIO_Port GPIOB
#define AC2_IN_Pin GPIO_PIN_9
#define AC2_IN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
