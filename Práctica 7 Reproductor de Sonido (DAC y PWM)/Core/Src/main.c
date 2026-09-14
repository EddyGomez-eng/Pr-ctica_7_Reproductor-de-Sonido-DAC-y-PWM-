/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Laboratorio 7 - Reproductor de audio por UART + DAC + PWM
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>      // Generamos la onda senoidal del DAC
#include <string.h>    // strlen() en UART
#include "audio1.h"   // Contiene las dos canciones y sus duraciones
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SINE_SIZE 128          // Numero de muestras de una onda senoidal completa
#define PI 3.1415926           // Constante PI
#define TIM6_FREQ 84000000UL   // Reloj de TIM6 = 84 MHz
#define TIM6_PSC 1UL           // Prescaler configurado para TIM6
#define DAC_MIDPOINT 2048      // Mitad de escala del DAC de 12 bits

/* TIM3 recibe 84 MHz y tiene PSC = 83:
 * 84 MHz / (83 + 1) = 1 MHz.
 * Este valor lo usamos para calcular el ARR de cada nota PWM.
 */
#define PWM_TIMER_FREQ 1000000UL
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
DAC_HandleTypeDef hdac;
DMA_HandleTypeDef hdma_dac1;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* Tabla de 128 muestras que DMA envia al DAC de forma circular. */
uint16_t Ysine[SINE_SIZE];

/* Guarda un caracter recibido desde la terminal serial. */
uint8_t rxData = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_DAC_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM6_Init(void);

/* USER CODE BEGIN PFP */
void generarSin(void);
uint32_t calcularARR_DAC(uint16_t freq);
void playToneDAC(const uint16_t *tone,
                 const uint16_t *duration,
                 uint16_t Nsize);
void noToneDAC(void);

void playTonePWM(const uint16_t *tone,
                 const uint16_t *duration,
                 uint16_t Nsize);
void noTonePWM(void);

void UART_SendString(const char *text);
void mostrarMenu(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Genera una senoide de 128 muestras con valores entre 0 y 4095. */
void generarSin(void)
{
    for (int x = 0; x < SINE_SIZE; x++)
    {
        Ysine[x] = (uint16_t)((sin((x * 2.0 * PI) / SINE_SIZE) + 1.0)
                              * (4095.0 / 2.0));
    }
}

/* Calcula el ARR de TIM6 necesario para producir una nota con el DAC. */
uint32_t calcularARR_DAC(uint16_t freq)
{
    if (freq == 0)
    {
        return 0;
    }

    /* TIM6 debe generar SINE_SIZE actualizaciones por cada ciclo de la nota. */
    uint32_t triggerFrequency = SINE_SIZE * (uint32_t)freq;

    return (TIM6_FREQ / ((TIM6_PSC + 1UL) * triggerFrequency)) - 1UL;
}

/* Detiene TIM6 para dejar de generar triggers hacia el DAC. */
void noToneDAC(void)
{
    HAL_TIM_Base_Stop(&htim6);

    /* Reinicia el contador para que la siguiente nota empiece limpia. */
    __HAL_TIM_SET_COUNTER(&htim6, 0);

    /* Deja cargado el valor medio del DAC durante el silencio. */
    HAL_DAC_SetValue(&hdac,
                     DAC_CHANNEL_1,
                     DAC_ALIGN_12B_R,
                     DAC_MIDPOINT);
}

/* Reproduce una secuencia usando TIM6 + DAC + DMA. */
void playToneDAC(const uint16_t *tone,
                 const uint16_t *duration,
                 uint16_t Nsize)
{
    for (uint16_t i = 0; i < Nsize; i++)
    {
        /* Frecuencia 0 representa silencio. */
        if (tone[i] == 0)
        {
            noToneDAC();
            HAL_Delay(duration[i]);
            continue;
        }

        /* Calcula el ARR correspondiente a la nota actual. */
        uint32_t valorARR = calcularARR_DAC(tone[i]);

        /* Cambia la frecuencia con la que TIM6 dispara el DAC. */
        __HAL_TIM_SET_AUTORELOAD(&htim6, valorARR);
        __HAL_TIM_SET_COUNTER(&htim6, 0);

        /* Inicia TIM6. Sus Update Events disparan el DAC. */
        HAL_TIM_Base_Start(&htim6);

        /* Mantiene la nota durante el tiempo indicado. */
        HAL_Delay(duration[i]);

        /* Detiene la nota antes de continuar. */
        noToneDAC();
    }
}

/* Detiene la salida PWM de TIM3 Channel 1. */
void noTonePWM(void)
{
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);

    /* Deja el duty cycle en 0 y reinicia el contador. */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COUNTER(&htim3, 0);
}

/* Reproduce una secuencia de notas usando PWM en TIM3 Channel 1. */
void playTonePWM(const uint16_t *tone,
                 const uint16_t *duration,
                 uint16_t Nsize)
{
    for (uint16_t i = 0; i < Nsize; i++)
    {
        /* Frecuencia 0 representa silencio. */
        if (tone[i] == 0)
        {
            noTonePWM();
            HAL_Delay(duration[i]);
            continue;
        }

        /* Con un timer de 1 MHz:
         * ARR = (1,000,000 / frecuencia) - 1
         */
        uint32_t valorARR = (PWM_TIMER_FREQ / (uint32_t)tone[i]) - 1UL;

        /* Duty cycle del 50 %. */
        uint32_t pulse = (valorARR + 1UL) / 2UL;

        /* Actualiza periodo y duty cycle para la nota actual. */
        __HAL_TIM_SET_AUTORELOAD(&htim3, valorARR);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse);
        __HAL_TIM_SET_COUNTER(&htim3, 0);

        /* Inicia PWM en TIM3 Channel 1. */
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);

        /* Mantiene la nota durante el tiempo indicado. */
        HAL_Delay(duration[i]);

        /* Detiene PWM antes de la siguiente nota. */
        noTonePWM();
    }
}

/* Envia una cadena completa por USART2. */
void UART_SendString(const char *text)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t *)text,
                      (uint16_t)strlen(text),
                      HAL_MAX_DELAY);
}

/* Menu de las canciones */
void mostrarMenu(void)
{
    UART_SendString("\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("        REPRODUCTOR DE AUDIO\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("1 - Audio 1: Ode to Joy (DAC)\r\n");
    UART_SendString("2 - Audio 2: Mario Theme (PWM)\r\n");
    UART_SendString("========================================\r\n");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* USER CODE BEGIN Init */
    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_DAC_Init();
    MX_USART2_UART_Init();
    MX_TIM3_Init();
    MX_TIM6_Init();

    /* USER CODE BEGIN 2 */

    /* Crea la tabla de 128 muestras de la senoide para el Audio 1. */
    generarSin();

    /*
     * Inicia DAC + DMA en modo circular.
     * TIM6 permanece detenido, por lo que el Audio 1 NO comienza aqui.
     * El audio empieza solamente cuando UART recibe el comando '1'.
     */
    if (HAL_DAC_Start_DMA(&hdac,
                          DAC_CHANNEL_1,
                          (uint32_t *)Ysine,
                          SINE_SIZE,
                          DAC_ALIGN_12B_R) != HAL_OK)
    {
        Error_Handler();
    }

    /* Ambos metodos de audio comienzan apagados. */
    noToneDAC();
    noTonePWM();

    /* Muestra el menu apenas inicia el programa. */
    mostrarMenu();

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */

        /* Espera un solo caracter desde la terminal serial. */
        if (HAL_UART_Receive(&huart2, &rxData, 1, HAL_MAX_DELAY) == HAL_OK)
        {
            switch (rxData)
            {
                case '1':
                    /* Audio 1: Ode to Joy por DAC. Duracion = 10 segundos. */
                    playToneDAC(audio1_freqs,
                                audio1_durations,
                                AUDIO1_LEN);

                    /* Limpia un posible Overrun si la terminal envio CR/LF. */
                    __HAL_UART_CLEAR_OREFLAG(&huart2);

                    /* Al terminar vuelve a mostrar el menu. */
                    mostrarMenu();
                    break;

                case '2':
                    /* Audio 2: Mario Theme por PWM. Duracion = 10 segundos. */
                    playTonePWM(audio2_freqs,
                                audio2_durations,
                                AUDIO2_LEN);

                    /* Limpia un posible Overrun si la terminal envio CR/LF. */
                    __HAL_UART_CLEAR_OREFLAG(&huart2);

                    /* Al terminar vuelve a mostrar el menu. */
                    mostrarMenu();
                    break;

                case '\r':
                case '\n':
                    /* Ignora ENTER y saltos de linea. */
                    break;

                default:
                    /* Cualquier otro caracter se ignora y no imprime texto extra. */
                    break;
            }
        }
        else
        {
            /* Limpia errores de recepcion y vuelve a esperar otro caracter. */
            __HAL_UART_CLEAR_OREFLAG(&huart2);
        }
    }
    /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    /** Initializes the RCC Oscillators according to the specified parameters
      * in the RCC_OscInitTypeDef structure.
      */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 16;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief DAC Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC_Init(void)
{
    /* USER CODE BEGIN DAC_Init 0 */
    /* USER CODE END DAC_Init 0 */

    DAC_ChannelConfTypeDef sConfig = {0};

    /* USER CODE BEGIN DAC_Init 1 */
    /* USER CODE END DAC_Init 1 */

    /** DAC Initialization */
    hdac.Instance = DAC;
    if (HAL_DAC_Init(&hdac) != HAL_OK)
    {
        Error_Handler();
    }

    /** DAC channel OUT1 config */
    sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;

    if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN DAC_Init 2 */
    /* USER CODE END DAC_Init 2 */
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{
    /* USER CODE BEGIN TIM3_Init 0 */
    /* USER CODE END TIM3_Init 0 */

    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    /* USER CODE BEGIN TIM3_Init 1 */
    /* USER CODE END TIM3_Init 1 */

    /* Asegura que TIM3 tenga reloj aunque el MSP generado no se haya actualizado. */
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 83;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 1000;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
    {
        Error_Handler();
    }

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* PWM Mode 1, duty inicial 50 %, polaridad alta. */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 500;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Configura PA6 como salida TIM3_CH1.
     * Se hace aqui para que este main sea autocontenido.
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USER CODE BEGIN TIM3_Init 2 */
    /* USER CODE END TIM3_Init 2 */
}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{
    /* USER CODE BEGIN TIM6_Init 0 */
    /* USER CODE END TIM6_Init 0 */

    TIM_MasterConfigTypeDef sMasterConfig = {0};

    /* USER CODE BEGIN TIM6_Init 1 */
    /* USER CODE END TIM6_Init 1 */

    htim6.Instance = TIM6;
    htim6.Init.Prescaler = 1;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = 1000;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    {
        Error_Handler();
    }

    /* Cada Update Event de TIM6 se usa como trigger del DAC. */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

    if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN TIM6_Init 2 */
    /* USER CODE END TIM6_Init 2 */
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
    /* USER CODE BEGIN USART2_Init 0 */
    /* USER CODE END USART2_Init 0 */

    /* USER CODE BEGIN USART2_Init 1 */
    /* USER CODE END USART2_Init 1 */

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN USART2_Init 2 */
    /* USER CODE END USART2_Init 2 */
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
    /* DMA controller clock enable */
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* DMA interrupt init */
    /* DMA1_Stream5_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* USER CODE BEGIN MX_GPIO_Init_1 */
    /* USER CODE END MX_GPIO_Init_1 */

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

    /* Configure GPIO pin : B1_Pin */
    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    /* Configure GPIO pin : LD2_Pin */
    GPIO_InitStruct.Pin = LD2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

    /* USER CODE BEGIN MX_GPIO_Init_2 */
    /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number. */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
