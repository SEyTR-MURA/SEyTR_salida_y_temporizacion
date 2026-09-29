# SEyTR · Salida y temporización

Proyecto de ejemplo de **Sistemas Empotrados y de Tiempo Real**. Es el de la
demostración de la sesión **U2-S1**, GPIO y temporización básica: un LED que
parpadea con un periodo fijo y unas trazas que dicen, en cada encendido, cuánto
se ha desviado el parpadeo de donde debería estar.

No hace falta cuenta ni permisos para clonarlo: el repositorio es público.

El trabajo propuesto de la sesión te pide escribir **tu propio** parpadeo, en
las dos versiones. Este proyecto no lo sustituye: sirve para repetir la
demostración y para comparar tus medidas con las suyas.

## Qué hace

Enciende el LED al principio de cada periodo y lo apaga a la mitad, y traza
cada encendido y cada apagado con una marca de tiempo en milisegundos, contada
desde el primer encendido. En cada encendido añade dos cifras:

- **periodo**: el tiempo transcurrido desde el encendido anterior;
- **desfase**: cuánto se ha desviado, en total, este encendido del instante en
  que debería haber llegado. El encendido número *n* debería llegar exactamente
  *n* periodos después del primero; si llega más tarde, el desfase es positivo.
  Si crece de un periodo a otro, el parpadeo **deriva**.

Mientras corre, eliges con una tecla **cómo espera** la tarea entre una
conmutación y la siguiente y **qué trabajo** hace dentro del bucle. No hay que
recompilar: basta con pulsar la tecla en el monitor.

## Qué necesitas antes

El entorno instalado y verificado con la guía de instalación de la asignatura,
la placa y su cable USB de datos, conectado al conector **USB-a-UART** de la
placa, el del puente USB-serie («USB-to-UART Port» en la guía del fabricante).
Por el otro conector, el USB nativo del ESP32-S3, verás las trazas, pero las
teclas no le llegan al programa. Conecta solo el cable del conector USB-a-UART.

El LED es opcional. Si lo tienes, móntalo con su resistencia en serie, de unos
cientos de ohmios, y con la placa desconectada. Sin LED el programa mide igual:
lo que se mide son los instantes, y salen en las trazas.

## Cómo obtenerlo

Clona el repositorio en una carpeta de tu equipo con una ruta **corta y sin
espacios ni tildes**, y abre en el editor la carpeta que contiene el
`CMakeLists.txt` de la raíz. Si abres una carpeta por encima o por debajo, el
entorno no reconoce el proyecto y el error no te dirá eso.

## Antes de compilar: el pin del LED

El pin del LED se declara en un solo sitio, `main/pines.h`, y el repositorio
llega **sin pin elegido**, porque elegirlo es parte del trabajo. Mientras no lo
cambies, el programa avisa al arrancar y hace las medidas sin mover ningún pin.

Para elegirlo, abre el esquema de tu placa y la tabla de pines de la hoja de
características del ESP32-S3, y descarta los que sirven a la memoria externa,
los de arranque, los del USB y la consola y los que la placa ya usa. Después
escribe su número en `PIN_LED` y anótalo en esta tabla:

| Señal | GPIO | Montaje |
| --- | --- | --- |
| LED | *sin elegir* | Entre el pin y masa, con su resistencia en serie |

Si montas el LED entre el pin y 3,3 V, se enciende con nivel bajo: cambia
también `LED_NIVEL_ENCENDIDO` a 0 en el mismo fichero.

## Cómo se usa

Compila, graba y abre el monitor. Al arrancar, el programa imprime la lista de
teclas y empieza con la espera relativa y sin trabajo en el bucle.

| Tecla | Qué hace |
| --- | --- |
| `r` | Espera relativa, con `vTaskDelay()` |
| `a` | Espera hasta instante absoluto, con `xTaskDelayUntil()` |
| `e` | Espera activa: un bucle que consulta la hora sin soltar la CPU |
| `0` | Sin trabajo en el bucle |
| `1` | 2 ms de cálculo en cada periodo |
| `2` | 20 ms de cálculo en cada periodo |
| `3` | Una traza larga en cada periodo |
| `z` | La medida vuelve a empezar |
| `h` | Vuelve a imprimir la lista |

Las teclas se pulsan sin Intro. **Cada tecla hace que la medida empiece de
cero**, de modo que el desfase que ves es siempre el de la variante que está
en marcha.

## Qué deberías ver

Una línea por encendido y otra por apagado. La del encendido tiene esta forma:

```
I (61234) parpadeo: #60 encendido t=60000.123 ms periodo=1000.004 ms desfase=+0.123 ms
```

El número entre paréntesis es la marca de tiempo del propio sistema de
registro, en milisegundos desde el arranque; las cifras que interesan son las
de detrás, que salen del reloj del sistema en microsegundos,
`esp_timer_get_time()`. Lo que
merece la pena mirar es cómo evoluciona el **desfase** a lo largo de unos
minutos:

- **Con la espera relativa y trabajo en el bucle**, el desfase crece en cada
  periodo: cada ciclo dura lo que se espera más lo que tarda el trabajo, y el
  error no se compensa, se acumula.
- **Con la espera hasta instante absoluto**, el desfase se queda en torno a
  cero con cualquiera de los tres trabajos: la tarea no espera un intervalo,
  espera al siguiente instante de la rejilla.
- **Sin trabajo en el bucle**, las dos esperas dan casi lo mismo. No es que la
  espera relativa sea buena: el tiempo se cuenta en tics, de un milisegundo con
  la configuración de este proyecto, y un cuerpo de bucle que tarda menos de un
  tic no llega a sumar ninguno. Por eso el trabajo artificial es de varios
  milisegundos.
- **Con la espera activa**, el LED sigue parpadeando, pero la tarea no suelta la
  CPU y en su núcleo no se ejecuta nada de menor prioridad, tampoco la tarea
  inactiva. A los pocos segundos aparece el aviso del *watchdog* de tareas,
  «Task watchdog got triggered», que nombra a la tarea inactiva del núcleo 0,
  `IDLE0`, como la que no ha podido ejecutarse, y a `parpadeo` como la que
  ocupaba ese núcleo. Esta espera cuenta desde el momento en que empieza, como
  la relativa, así que también deriva. Vuelve con `r` o con `a`.

Si el trabajo dentro del bucle tarda más que el tiempo que hay que esperar, la
espera hasta instante absoluto ya no puede recuperarlo: el programa traza
«activacion tardia». Lo puedes provocar con los 20 ms de cálculo y un periodo
de 30 ms: la mitad encendida, 15 ms, ya no da para el trabajo.

Si el monitor muestra las trazas pero no responde a las teclas, mira el
conector: tiene que ser el USB-a-UART.

## Qué puedes cambiar sin tocar el código

En la configuración del proyecto, en el menú **«SEyTR - Salida y
temporizacion»**, que está en el nivel superior, junto a los de ESP-IDF, y no
dentro de la configuración de componentes:

| Opción | Qué hace |
| --- | --- |
| Periodo del parpadeo | El tiempo entre dos encendidos, en milisegundos. La mitad encendido, la otra mitad apagado |

El proyecto fija el tic del sistema operativo a 1000 Hz en
`sdkconfig.defaults`, como todos los de la asignatura. Con los 100 Hz que trae
ESP-IDF por omisión, el tic dura 10 ms y un trabajo de 2 ms no llega a
notarse. Si el programa avisa al arrancar de que el tic no está a 1000 Hz, tu
`sdkconfig` trae otro valor, y `sdkconfig.defaults` no cambia las opciones que
ya están en él: borra el fichero `sdkconfig` y vuelve a fijar el destino.

## Estructura

```
CMakeLists.txt              define el proyecto
sdkconfig.defaults          configuración de partida; se escribe a mano y se versiona
main/
  CMakeLists.txt            registra las fuentes del componente principal
  Kconfig.projbuild         la opción de la tabla de arriba
  pines.h                   el pin del LED y su montaje, en un solo sitio
  salida_y_temporizacion.c  el programa: el parpadeo, las trazas y las teclas
components/
  led/                      componente propio: el LED
```

El componente `led` no sabe en qué pin está el LED: se lo dice `main` al
iniciarlo. Su `README.md` explica cómo se usa.

Hay dos cosas que **no** se versionan y que por eso no están aquí: el fichero
`sdkconfig`, que genera la herramienta a partir de `sdkconfig.defaults`, y la
carpeta `build/`.

## Un aviso

Este repositorio es material de la asignatura. El repositorio de tu **proyecto
final** es otro distinto, con su propio nombre y sus propias reglas, y llega más
adelante en el curso.
