# CMakeLists.txt de componente. Registra las fuentes y las cabeceras publicas.
# Si anades un fichero .c, anadelo tambien a SRCS.
# PRIV_REQUIRES dice de que componentes depende este: el LED del propio
# proyecto y esp_timer, que da el reloj del sistema en microsegundos con el que
# se toman las marcas de tiempo.
idf_component_register(SRCS "salida_y_temporizacion.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES led esp_timer)
