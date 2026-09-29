/* salida_y_temporizacion.c - SEyTR, Unidad 2.
 *
 * Qué hace este programa
 * ----------------------
 * Hace parpadear un LED con un periodo fijo y traza, con marca de tiempo, cada
 * encendido y cada apagado. En cada encendido dice cuánto ha durado el último
 * periodo y cuánto se ha desviado el parpadeo, en total, de la rejilla ideal:
 * el encendido número n debería llegar exactamente n periodos después del
 * primero. Esa desviación acumulada es la deriva.
 *
 * Desde el monitor, sin recompilar, se elige cómo espera la tarea entre una
 * conmutación y la siguiente y qué trabajo hace dentro del bucle. Las teclas
 * se listan al arrancar y con «h».
 *
 * Qué hardware necesita
 * ---------------------
 * La placa del curso, conectada por su conector USB-a-UART: es por donde
 * llegan las teclas. El LED, con su resistencia en serie, es opcional: su pin
 * se declara en pines.h, y sin LED el programa mide igual.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "led.h"
#include "pines.h"

static const char *TAG = "parpadeo";

/* Periodo del parpadeo, de la configuración del proyecto. La mitad encendido
 * y la otra mitad apagado. */
#define PERIODO_MS    CONFIG_SEYTR_PERIODO_MS
#define ENCENDIDO_MS  (PERIODO_MS / 2)
#define APAGADO_MS    (PERIODO_MS - ENCENDIDO_MS)

/* Longitud de la traza larga, en caracteres. */
#define LARGO_TRAZA   600

/* Cómo espera la tarea entre una conmutación y la siguiente. */
typedef enum {
    ESPERA_RELATIVA,  /* vTaskDelay(): un intervalo contado desde ahora */
    ESPERA_ABSOLUTA,  /* xTaskDelayUntil(): hasta el siguiente instante de la rejilla */
    ESPERA_ACTIVA,    /* un bucle que consulta la hora sin soltar la CPU */
} espera_t;

/* Trabajo artificial que la tarea hace después de cada encendido. */
typedef enum {
    CARGA_NINGUNA,
    CARGA_2_MS,
    CARGA_20_MS,
    CARGA_TRAZA_LARGA,
} carga_t;

typedef struct {
    espera_t espera;
    carga_t  carga;
} variante_t;

static const char *const NOMBRE_ESPERA[] = {
    [ESPERA_RELATIVA] = "relativa, con vTaskDelay()",
    [ESPERA_ABSOLUTA] = "hasta instante absoluto, con xTaskDelayUntil()",
    [ESPERA_ACTIVA]   = "activa, sin soltar la CPU",
};

static const char *const NOMBRE_CARGA[] = {
    [CARGA_NINGUNA]     = "ninguno",
    [CARGA_2_MS]        = "2 ms de calculo",
    [CARGA_20_MS]       = "20 ms de calculo",
    [CARGA_TRAZA_LARGA] = "una traza larga",
};

/* Lo que se va midiendo desde el último cambio de variante. */
typedef struct {
    uint32_t n;            /* encendidos trazados */
    int64_t  origen_us;    /* instante del primer encendido */
    int64_t  anterior_us;  /* instante del encendido anterior */
} medida_t;

/* Variante con la que arranca el programa. */
static const variante_t VARIANTE_INICIAL = { ESPERA_RELATIVA, CARGA_NINGUNA };

/* La tarea del teclado manda a la de parpadeo la variante que tiene que usar.
 * Se hace con una cola, que es la forma de pasar datos de una tarea a otra:
 * se usa en U2-S2 y se estudia en la U4. Esta es de un solo elemento y se
 * escribe con xQueueOverwrite(), que sustituye lo que haya: si llegan dos
 * teclas dentro del mismo periodo, cuenta la última y no se pierde ninguna. */
static QueueHandle_t s_variantes;

static led_t s_led;
static char s_traza_larga[LARGO_TRAZA + 1];

/* Escribe en texto una duración dada en microsegundos, como milisegundos con
 * tres decimales. Se hace con enteros para no depender del formato de coma
 * flotante de la biblioteca estándar. */
static const char *en_ms(int64_t us, bool con_signo, char *texto, size_t tamano)
{
    const int64_t absoluto = (us < 0) ? -us : us;
    const char *signo = (us < 0) ? "-" : (con_signo ? "+" : "");
    snprintf(texto, tamano, "%s%lld.%03lld", signo,
             (long long)(absoluto / 1000), (long long)(absoluto % 1000));
    return texto;
}

static void trazar_encendido(medida_t *m, int64_t ahora_us)
{
    if (m->n == 0) {
        m->origen_us = ahora_us;
        ESP_LOGI(TAG, "#0 encendido t=0.000 ms, origen de la medida");
    } else {
        const int64_t t_us = ahora_us - m->origen_us;
        const int64_t ideal_us = (int64_t)m->n * PERIODO_MS * 1000;
        char t[24];
        char periodo[24];
        char desfase[24];
        ESP_LOGI(TAG, "#%lu encendido t=%s ms periodo=%s ms desfase=%s ms",
                 (unsigned long)m->n,
                 en_ms(t_us, false, t, sizeof t),
                 en_ms(ahora_us - m->anterior_us, false, periodo, sizeof periodo),
                 en_ms(t_us - ideal_us, true, desfase, sizeof desfase));
    }
    m->anterior_us = ahora_us;
    m->n++;
}

static void trazar_apagado(const medida_t *m, int64_t ahora_us)
{
    char t[24];
    ESP_LOGI(TAG, "#%lu apagado   t=%s ms",
             (unsigned long)(m->n - 1), en_ms(ahora_us - m->origen_us, false, t, sizeof t));
}

/* El trabajo artificial. Los dos cálculos son una espera activa de duración
 * fija, que para esto hace el mismo papel que un cálculo de verdad y dura
 * siempre lo mismo. La traza larga dura lo que tarda la consola en escribirla:
 * la UART envía unos 11,5 caracteres por milisegundo a 115200 baudios y, cuando
 * su memoria de 128 caracteres se llena, la escritura se queda dando vueltas,
 * en espera activa, hasta que queda hueco para el carácter siguiente. */
static void trabajo_artificial(carga_t carga)
{
    switch (carga) {
    case CARGA_2_MS:
        esp_rom_delay_us(2 * 1000);
        break;
    case CARGA_20_MS:
        esp_rom_delay_us(20 * 1000);
        break;
    case CARGA_TRAZA_LARGA:
        ESP_LOGI(TAG, "traza larga: %s", s_traza_larga);
        break;
    case CARGA_NINGUNA:
    default:
        break;
    }
}

/* Espera duracion_ms de la forma elegida. referencia solo la usa la espera
 * hasta instante absoluto, y la actualiza la propia xTaskDelayUntil(). */
static void esperar(espera_t espera, TickType_t *referencia, uint32_t duracion_ms)
{
    switch (espera) {
    case ESPERA_RELATIVA:
        vTaskDelay(pdMS_TO_TICKS(duracion_ms));
        break;

    case ESPERA_ABSOLUTA:
        /* Devuelve pdFALSE si no ha llegado a esperar porque el instante
         * pedido ya había pasado: el cuerpo del bucle ha tardado más que el
         * intervalo, y la activación llega tarde. */
        if (xTaskDelayUntil(referencia, pdMS_TO_TICKS(duracion_ms)) == pdFALSE) {
            ESP_LOGW(TAG, "activacion tardia: el instante ya habia pasado");
        }
        break;

    case ESPERA_ACTIVA: {
        /* La tarea no se bloquea: consulta la hora hasta que llega. Mientras
         * tanto, en su núcleo no se ejecuta ninguna tarea de prioridad menor,
         * tampoco la tarea inactiva, y a los pocos segundos avisa el
         * watchdog de tareas. Cuenta desde ahora, como vTaskDelay(), así que
         * también deriva, aunque sin redondear a tics. */
        const int64_t fin_us = esp_timer_get_time() + (int64_t)duracion_ms * 1000;
        while (esp_timer_get_time() < fin_us) {
            /* nada */
        }
        break;
    }

    default:
        break;
    }
}

static void anunciar(const variante_t *v)
{
    ESP_LOGI(TAG, "--- espera %s; trabajo en el bucle: %s; periodo %d ms",
             NOMBRE_ESPERA[v->espera], NOMBRE_CARGA[v->carga], PERIODO_MS);
    ESP_LOGI(TAG, "--- la medida empieza de cero");
}

static void tarea_parpadeo(void *argumento)
{
    (void)argumento;

    variante_t variante = VARIANTE_INICIAL;
    medida_t medida = { 0 };
    TickType_t referencia = 0;
    bool empezar = true;

    for (;;) {
        variante_t nueva;
        if (xQueueReceive(s_variantes, &nueva, 0) == pdTRUE) {
            variante = nueva;
            empezar = true;
        }

        if (empezar) {
            anunciar(&variante);
            /* Deja que la consola termine de escribir el aviso, para que el
             * tiempo que tarda no entre en la primera medida. */
            vTaskDelay(pdMS_TO_TICKS(100));
            medida = (medida_t){ 0 };
            /* La referencia de xTaskDelayUntil() se ancla aquí, cuando la
             * medida empieza de cero. Dentro del bucle no se toca: la
             * actualiza la propia función. */
            referencia = xTaskGetTickCount();
            empezar = false;
        }

        const int64_t encendido_us = esp_timer_get_time();
        led_encender(&s_led);
        trazar_encendido(&medida, encendido_us);
        trabajo_artificial(variante.carga);
        esperar(variante.espera, &referencia, ENCENDIDO_MS);

        const int64_t apagado_us = esp_timer_get_time();
        led_apagar(&s_led);
        trazar_apagado(&medida, apagado_us);
        esperar(variante.espera, &referencia, APAGADO_MS);
    }
}

static void mostrar_ayuda(void)
{
    printf("\n"
           "Teclas (con el monitor abierto, sin pulsar Intro):\n"
           "  r  espera relativa, con vTaskDelay()\n"
           "  a  espera hasta instante absoluto, con xTaskDelayUntil()\n"
           "  e  espera activa, sin soltar la CPU\n"
           "  0  sin trabajo en el bucle\n"
           "  1  2 ms de calculo en cada periodo\n"
           "  2  20 ms de calculo en cada periodo\n"
           "  3  una traza larga en cada periodo\n"
           "  z  la medida vuelve a empezar\n"
           "  h  esta ayuda\n"
           "Cada tecla hace que la medida empiece de cero.\n\n");
}

static void tarea_teclado(void *argumento)
{
    variante_t variante = VARIANTE_INICIAL;

    for (;;) {
        /* Mientras no se instale el controlador de la UART, y aquí no se
         * instala, read() sobre la consola no se queda esperando: si no ha
         * llegado nada, devuelve -1 enseguida. Por eso se consulta cada 20 ms.
         * Aquí el ritmo no importa, y una espera relativa basta. */
        char tecla;
        if (read(STDIN_FILENO, &tecla, 1) == 1) {
            bool enviar = true;
            switch (tecla) {
            case 'r': variante.espera = ESPERA_RELATIVA;   break;
            case 'a': variante.espera = ESPERA_ABSOLUTA;   break;
            case 'e': variante.espera = ESPERA_ACTIVA;     break;
            case '0': variante.carga  = CARGA_NINGUNA;     break;
            case '1': variante.carga  = CARGA_2_MS;        break;
            case '2': variante.carga  = CARGA_20_MS;       break;
            case '3': variante.carga  = CARGA_TRAZA_LARGA; break;
            case 'z':                                      break;
            case 'h':
            case '?': mostrar_ayuda();                     break;
            default:  enviar = false;                      break;
            }
            if (enviar) {
                xQueueOverwrite(s_variantes, &variante);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SEyTR - salida y temporizacion. Periodo %d ms, tic a %d Hz",
             PERIODO_MS, configTICK_RATE_HZ);

    if (configTICK_RATE_HZ != 1000) {
        ESP_LOGW(TAG, "el tic no esta a 1000 Hz: un trabajo de menos de un tic "
                      "no se nota en la espera relativa");
    }

    ESP_ERROR_CHECK(led_iniciar(&s_led, PIN_LED, LED_NIVEL_ENCENDIDO));
    if (PIN_LED == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "no hay pin para el LED: eligelo en main/pines.h. "
                      "Mientras tanto, las medidas salen igual en las trazas");
    }

    memset(s_traza_larga, '.', LARGO_TRAZA);
    s_traza_larga[LARGO_TRAZA] = '\0';

    s_variantes = xQueueCreate(1, sizeof(variante_t));
    if (s_variantes == NULL) {
        ESP_LOGE(TAG, "no hay memoria para la cola");
        return;
    }

    mostrar_ayuda();

    /* Dos tareas: las tareas, sus prioridades y su reparto entre los núcleos
     * se estudian en la U3. La de parpadeo va fijada al núcleo 0 para que la
     * espera activa deje sin CPU siempre a la misma tarea inactiva, la de ese
     * núcleo. La del teclado tiene más prioridad, para que una tecla se
     * atienda enseguida aunque las dos coincidan en el mismo núcleo. */
    const BaseType_t parpadeo =
        xTaskCreatePinnedToCore(tarea_parpadeo, "parpadeo", 4096, NULL, 5, NULL, 0);
    const BaseType_t teclado =
        xTaskCreate(tarea_teclado, "teclado", 3072, NULL, 6, NULL);
    if (parpadeo != pdPASS || teclado != pdPASS) {
        ESP_LOGE(TAG, "no hay memoria para las tareas");
        return;
    }

    /* app_main termina aquí; las dos tareas siguen. */
}
