/* config.c - Estructuras y funciones de configuracion para IALearner
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "config.h"



/* Elimina espacios y saltos de linea al inicio y final de un string. */
static void trim(char *s) {
    char *inicio = s;
    while (*inicio && isspace((unsigned char)*inicio)) inicio++;

    char *fin = inicio + strlen(inicio);
    while (fin > inicio && isspace((unsigned char)*(fin - 1))) fin--;
    *fin = '\0';

    /* Si habia espacios a la izquierda mueve el puntero original*/
    if (inicio != s) memmove(s, inicio, fin - inicio + 1);
}

/* Convierte un string a minusculas */
static void aMinusculas(char *s) {
    while (*s) {
        *s = tolower((unsigned char)*s);
        s++;
    }
}

/* Calcula si un numero es primo */
static int esPrimo(int n) {
    if (n == 2) return 1;
    if (n < 2 || n % 2 == 0) return 0;

    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return 0;
    }
    return 1;
}

/* Retorna el primer primo >= n */
static int siguientePrimo(int n) {
    if (n < 2) return 2;
    while (!esPrimo(n)) {
        n++;
    }
    return n;
}

/* Funcion hash FNV-1a, case-insensitive (opera sobre minusculas) */
static unsigned long hashFNV1a(const char *str, int tam_tabla) {
    unsigned long hash = 2166136261UL; // offset basis
    int c;
    while ((c = *str++)) {
        hash ^= (unsigned long)tolower((unsigned char)c); // XOR primero
        hash *= 16777619UL; // luego multiplica
    }
    return hash % (unsigned long)tam_tabla;
}

/* Busca el indice de un TipoDocumento por nombre (en mayusculas). Si no existe lo crea
 y le asigna un indice en tipos[]. Retorna el indice en config->tipos[] o -1 si hay un
 error al momento de usar realloc. */
static int obtenerOCrearTipo(const char *nombre, ConfigIALearner *config) {
    /* Buscamos si ya existe */
    for (int i = 0; i < config->num_tipos; i++) {
        if (strcasecmp(config->tipos[i].nombre, nombre) == 0) {
            return i;
        }
    }

    /* No existe, creamos uno nuevo */
    if (config->num_tipos >= config->cap_tipos) {
        int nueva_cap = config->cap_tipos * 2;
        TipoDocumento *tmp = realloc(config->tipos, nueva_cap * sizeof(TipoDocumento));
        if (!tmp) {
            fprintf(stderr, "[config] Error realloc arreglo tipos documentos\n");
            return -1;
        }
        config->tipos = tmp;
        config->cap_tipos = nueva_cap;
    }

    TipoDocumento *nuevo = &config->tipos[config->num_tipos];
    memset(nuevo, 0, sizeof(TipoDocumento));
    strncpy(nuevo->nombre, nombre, sizeof(nuevo->nombre) - 1);
    nuevo->nombre[sizeof(nuevo->nombre) - 1] = '\0';
    nuevo->num_palabras = 0;

    return config->num_tipos++;
}

/* Agrega indice_tipo a los tipos de una entrada existente, si no lo tenia ya.
 Retorna 0 en exito (incluso si ya estaba), -1 si falla el realloc. */
static int agregarTipoAEntrada(EntradaHash *entrada, int indice_tipo) {
    for (int i = 0; i < entrada->num_indices; i++) {
        if (entrada->indices_tipo[i] == indice_tipo) return 0; // ya pertenece a este tipo
    }
    if (entrada->num_indices >= entrada->cap_indices) {
        int nueva_cap = entrada->cap_indices * 2;
        int *tmp = realloc(entrada->indices_tipo, nueva_cap * sizeof(int));
        if (!tmp) return -1;
        entrada->indices_tipo = tmp;
        entrada->cap_indices = nueva_cap;
    }
    entrada->indices_tipo[entrada->num_indices++] = indice_tipo;
    return 0;
}

/* Inserta una palabra en la tabla hash. Si la palabra ya existe (de otro
 dicc con distinto tipo, o repetida), se le agrega el nuevo tipo.
 Retorna 0 en exito, -1 en error. */
static int insertarEnHash(const char *palabra, int indice_tipo, ConfigIALearner *config) {
    char *palabra_min = malloc(strlen(palabra) + 1);
    if (palabra_min == NULL) {
        fprintf(stderr, "[config] Error malloc\n");
        return -1;
    }
    strcpy(palabra_min, palabra);
    aMinusculas(palabra_min);

    unsigned long cubeta = hashFNV1a(palabra_min, config->tam_hash);

    EntradaHash *actual = config->tabla_hash[cubeta];
    while (actual) {
        if (strcmp(actual->palabra, palabra_min) == 0) {
            // La palabra ya existe, le sumamos este tipo, o no si ya lo tenia
            int r = agregarTipoAEntrada(actual, indice_tipo);
            free(palabra_min);
            if (r < 0) {
                fprintf(stderr, "[config] Error realloc indices_tipo para '%s'\n", actual->palabra);
                return -1;
            }
            return 0;
        }
        actual = actual->siguiente;
    }

    // No existe, se crea una entrada nueva con este primer tipo
    EntradaHash *nueva = malloc(sizeof(EntradaHash));
    if (!nueva) {
        fprintf(stderr, "[config] Error malloc EntradaHash\n");
        free(palabra_min);
        return -1;
    }
    nueva->cap_indices = 2;
    nueva->indices_tipo = malloc(nueva->cap_indices * sizeof(int));
    if (!nueva->indices_tipo) {
        fprintf(stderr, "[config] Error malloc indices_tipo\n");
        free(palabra_min);
        free(nueva);
        return -1;
    }
    nueva->indices_tipo[0] = indice_tipo;
    nueva->num_indices = 1;
    nueva->palabra = palabra_min;
    nueva->siguiente = config->tabla_hash[cubeta];
    config->tabla_hash[cubeta] = nueva;

    return 0;
}

int parsearDiccionarios(const char *ruta, ConfigIALearner *config) {
    FILE *f = fopen(ruta, "r");
    if (!f) {
        fprintf(stderr, "[config] No se pudo abrir '%s': ", ruta);
        perror("");
        return -1;
    }

    // Arrays dinamicos con capacidad inicial inicializados
    config->num_tipos = 0;
    config->cap_tipos = 4;
    config->tipos = malloc(config->cap_tipos * sizeof(TipoDocumento));
    if (!config->tipos) {
        fprintf(stderr, "[config] Error malloc inicializacion arreglo tipos\n");
        fclose(f);
        return -1;
    }

    char linea[256] = {0};

    // Primero se cuenta el total de palabras de los dicc para calcular el tam_hash optimo
    int total_palabras = 0;
    while (fgets(linea, sizeof(linea), f)) {
        trim(linea);
        if (strncmp(linea, "PALABRA ", 8) == 0) total_palabras++;
    }
    rewind(f); // Volvemos al inicio del archivo

    // Calculamos el tam_hash optimo: primer primo >= total_palabras / 0.7 (factor de carga)
    int tam_minimo = 0;
    if (total_palabras > 0) {
        tam_minimo = (int)(total_palabras / 0.7) + 1;
    } else {
        tam_minimo = 16;
    }
    config->tam_hash = siguientePrimo(tam_minimo);

    config->tabla_hash = calloc(config->tam_hash, sizeof(EntradaHash *));
    if (!config->tabla_hash) {
        fprintf(stderr, "[config] Error calloc tabla_hash\n");
        fclose(f);
        return -1;
    }

    // Luego parseamos el contenido del archivo de config
    int tipo_actual = -1; // indice del tipo en construccion
    int num_linea = 0;
    int dentro_diccionario = 0; // 1 cuando lee DICCIONARIO
    int tipo_declarado = 0; // 1 si se declaro TIPO dentro del diccionario actual

    while (fgets(linea, sizeof(linea), f)) {
        num_linea++;
        trim(linea);

        // Ignora lineas vacias y comentarios
        if (linea[0] == '\0' || linea[0] == '#') continue;

        if (strncmp(linea, "DICCIONARIO ", 12) == 0) {
            if (dentro_diccionario) {
                fprintf(stderr, "[config] Linea %d: doble declaración de DICCIONARIO, anidación no permitida\n", num_linea);
                fclose(f);
                return -1;
            }

            dentro_diccionario = 1;
            tipo_actual = -1;
            tipo_declarado = 0;
            // El nombre del diccionario es informativo no se necesita
            continue;
        }

        if (strncmp(linea, "TIPO ", 5) == 0) {
            if (!dentro_diccionario) {
                fprintf(stderr, "[config] Linea %d: TIPO fuera de un bloque DICCIONARIO\n", num_linea);
                fclose(f);
                return -1;
            }
            if (tipo_declarado) {
                fprintf(stderr, "[config] Linea %d: TIPO ya está declarado en el mismo diccionario\n", num_linea);
                fclose(f);
                return -1;
            }

            char nombre_tipo[64] = {0};
            strncpy(nombre_tipo, linea + 5, sizeof(nombre_tipo) - 1);
            trim(nombre_tipo);
            if (strlen(nombre_tipo) == 0) {
                fprintf(stderr, "[config] Linea %d: TIPO sin nombre definido\n", num_linea);
                fclose(f);
                return -1;
            }

            tipo_actual = obtenerOCrearTipo(nombre_tipo, config);
            if (tipo_actual < 0) {
                fclose(f); 
                return -1;
            }
            tipo_declarado = 1;
            continue;
        }

        if (strncmp(linea, "PALABRA ", 8) == 0) {
            if (!dentro_diccionario) {
                fprintf(stderr, "[config] Linea %d: PALABRA fuera de un bloque DICCIONARIO\n", num_linea);
                fclose(f);
                return -1;
            }
            if (!tipo_declarado) {
                fprintf(stderr, "[config] Linea %d: PALABRA antes de declarar TIPO en este diccionario\n", num_linea);
                fclose(f);
                return -1;
            }

            char palabra[128] = {0};
            strncpy(palabra, linea + 8, sizeof(palabra) - 1);
            trim(palabra);
            if (strlen(palabra) == 0) {
                fprintf(stderr, "[config] Linea %d: PALABRA vacia\n", num_linea);
                continue;
            }

            if (insertarEnHash(palabra, tipo_actual, config) < 0) {
                fclose(f);
                return -1;
            }
            config->tipos[tipo_actual].num_palabras++;
            continue;
        }

        if (strcmp(linea, "FIN_DICCIONARIO") == 0) {
            if (!dentro_diccionario) {
                fprintf(stderr, "[config] Linea %d: FIN_DICCIONARIO sin DICCIONARIO previo\n", num_linea);
                fclose(f);
                return -1;
            }
            if (!tipo_declarado) {
                fprintf(stderr, "[config] Linea %d: diccionario sin TIPO declarado\n", num_linea);
                fclose(f);
                return -1;
            }

            dentro_diccionario = 0;
            tipo_actual = -1;
            tipo_declarado = 0;
            continue;
        }

        fprintf(stderr, "[config] Linea %d: token desconocido '%s'\n", num_linea, linea);
    }

    if (dentro_diccionario) {
        fprintf(stderr, "[config] Error: archivo termina sin FIN_DICCIONARIO\n");
        fclose(f);
        return -1;
    }

    if (config->num_tipos == 0) {
        fprintf(stderr, "[config] Error: no se definio ningun tipo de documento (ventana)\n");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int parsearReglas(const char *ruta, ConfigIALearner *config) {
    FILE *f = fopen(ruta, "r");
    if (!f) {
        fprintf(stderr, "[config] No se pudo abrir '%s': ", ruta);
        perror("");
        return -1;
    }

    config->num_reglas = 0;
    config->cap_reglas = 4;
    config->reglas = malloc(config->cap_reglas * sizeof(ReglaUsuario));
    if (!config->reglas) {
        fprintf(stderr, "[config] Error malloc reglas de usurio iniciales\n");
        fclose(f);
        return -1;
    }

    // Valores por defecto por si no se definen en el archivo
    strncpy(config->delimitadores, " ,./|:;'", sizeof(config->delimitadores) - 1);
    config->umbral_ocurrencias = 3;

    char linea[256];
    int num_linea = 0;
    int dentro_usuario = 0;
    ReglaUsuario *regla_actual = NULL;
    int cap_condiciones_actual = 0;

    while (fgets(linea, sizeof(linea), f)) {
        num_linea++;
        trim(linea);
        if (linea[0] == '\0' || linea[0] == '#') continue;

        if (strncmp(linea, "DELIMITADORES ", 14) == 0) {
            if (dentro_usuario) {
                fprintf(stderr, "[config] Linea %d: DELIMITADORES dentro de bloque USUARIO\n", num_linea);
                fclose(f);
                return -1;
            }

            // Los delimitadores deben ir dentro de comillas " ,./|:;'"
            char *inicio = strchr(linea + 14, '"'); // Primera aparicion de "
            char *fin = NULL;
            if (inicio) {
                fin = strrchr(linea + 14, '"'); // Ultima aparicion de "
            }

            if (!inicio || fin == inicio) {
                fprintf(stderr, "[config] Linea %d: DELIMITADORES debe estar entre comillas\n", num_linea);
                fclose(f);
                return -1;
            }

            int largo = fin - inicio - 1;
            if (largo <= 0 || largo >= (int) sizeof(config->delimitadores)) {
                fprintf(stderr, "[config] Linea %d: DELIMITADORES vacio o demasiado largo\n", num_linea);
                fclose(f);
                return -1;
            }

            strncpy(config->delimitadores, inicio + 1, largo);
            config->delimitadores[largo] = '\0';
            continue;
        }

        if (strncmp(linea, "UMBRAL_OCURRENCIAS ", 19) == 0) {
            int umbral = atoi(linea + 19);
            if (umbral <= 0) {
                fprintf(stderr, "[config] Linea %d: UMBRAL_OCURRENCIAS debe ser un numero entero valido > 0\n", num_linea);
                fclose(f);
                return -1;
            }
            config->umbral_ocurrencias = umbral;
            continue;
        }

        if (strncmp(linea, "USUARIO ", 8) == 0) {
            if (dentro_usuario) {
                fprintf(stderr, "[config] Linea %d: USUARIO definido anteriormente sin cierre\n", num_linea);
                fclose(f);
                return -1;
            }

            // Primero verificamos si no se ha definido un usuario con este nombre
            int es_duplicado = 0;
            for (int i = 0; i < config->num_reglas; i++) {
                if (strcasecmp(config->reglas[i].nombre, linea + 8) == 0) {
                    fprintf(stderr, "[config] Linea %d: USUARIO '%s' ya definido, se omite\n", num_linea, linea + 8);
                    es_duplicado = 1;
                    break;
                }
            }
            if (es_duplicado) continue;
            

            // Se aumenta tam al array de reglas si hace falta
            if (config->num_reglas >= config->cap_reglas) {
                int nueva_cap = config->cap_reglas * 2;
                ReglaUsuario *tmp = realloc(config->reglas, nueva_cap * sizeof(ReglaUsuario));
                if (!tmp) {
                    fprintf(stderr, "[config] Error realloc reglas de usuario\n");
                    fclose(f);
                    return -1;
                }
                config->reglas = tmp;
                config->cap_reglas = nueva_cap;
            }

            regla_actual = &config->reglas[config->num_reglas];
            memset(regla_actual, 0, sizeof(ReglaUsuario));
            strncpy(regla_actual->nombre, linea + 8, sizeof(regla_actual->nombre) - 1);
            trim(regla_actual->nombre);

            cap_condiciones_actual = 4;
            regla_actual->condiciones = malloc(cap_condiciones_actual * sizeof(CondicionUsuario));
            if (!regla_actual->condiciones) {
                fprintf(stderr, "[config] Error malloc condiciones de regla de usuario\n");
                fclose(f);
                return -1;
            }
            regla_actual->num_condiciones = 0;
            dentro_usuario = 1;
            continue;
        }

        if (strncmp(linea, "CONDICION ", 10) == 0) {
            if (!dentro_usuario) {
                fprintf(stderr, "[config] Linea %d: CONDICION fuera de bloque USUARIO\n", num_linea);
                fclose(f);
                return -1;
            }

            char nombre_tipo[64] = {0};
            double proporcion = 0.0;
            if (sscanf(linea + 10, "%63s %lf", nombre_tipo, &proporcion) != 2) {
                fprintf(stderr, "[config] Linea %d: formato invalido en CONDICION (esperado: CONDICION TIPO proporcion)\n", num_linea);
                fclose(f);
                return -1;
            }
            if (proporcion <= 0.0 || proporcion > 1.0) {
                fprintf(stderr, "[config] Linea %d: proporcion %.2f invalida (debe ser entre 0.0 y 1.0)\n", num_linea, proporcion);
                fclose(f);
                return -1;
            }

            // Buscamos el tipo por nombre en los tipos ya cargados
            int idx_tipo = -1;
            for (int i = 0; i < config->num_tipos; i++) {
                if (strcasecmp(config->tipos[i].nombre, nombre_tipo) == 0) {
                    idx_tipo = i;
                    break;
                }
            }
            if (idx_tipo < 0) {
                fprintf(stderr, "[config] Linea %d: tipo '%s' en CONDICION no existe en el archivo de config. de diccionarios proporcionado\n", num_linea, nombre_tipo);
                fclose(f);
                return -1;
            }

            // Aumentar tam de array condiciones si hace falta
            if (regla_actual->num_condiciones >= cap_condiciones_actual) {
                cap_condiciones_actual *= 2;
                CondicionUsuario *tmp = realloc(regla_actual->condiciones, cap_condiciones_actual * sizeof(CondicionUsuario));
                if (!tmp) {
                    fprintf(stderr, "[config] Error realloc condiciones\n");
                    fclose(f);
                    return -1;
                }
                regla_actual->condiciones = tmp;
            }

            regla_actual->condiciones[regla_actual->num_condiciones].indice_tipo = idx_tipo;
            regla_actual->condiciones[regla_actual->num_condiciones].proporcion_min = proporcion;
            regla_actual->num_condiciones++;
            continue;
        }

        if (strcmp(linea, "FIN_USUARIO") == 0) {
            if (!dentro_usuario) {
                fprintf(stderr, "[config] Linea %d: FIN_USUARIO sin USUARIO previo\n", num_linea);
                fclose(f);
                return -1;
            }
            if (regla_actual->num_condiciones == 0) {
                fprintf(stderr, "[config] Linea %d: usuario '%s' sin condiciones\n", num_linea, regla_actual->nombre);
                fclose(f);
                return -1;
            }

            config->num_reglas++;
            dentro_usuario = 0;
            regla_actual = NULL;
            continue;
        }

        fprintf(stderr, "[config] Linea %d: token desconocido '%s'\n", num_linea, linea);
    }

    if (dentro_usuario) {
        fprintf(stderr, "[config] Error: archivo termina sin FIN_USUARIO\n");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int aplicarPalabraAFrecuencias(const char *palabra, ConfigIALearner *config, int *frecuencias, int num_tipos) {
    if (!palabra || !config->tabla_hash) return 0;

    char buf[256];
    strncpy(buf, palabra, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (int i = 0; buf[i]; i++) buf[i] = tolower((unsigned char)buf[i]);

    unsigned long cubeta = hashFNV1a(buf, config->tam_hash);
    EntradaHash *actual = config->tabla_hash[cubeta];

    while (actual) {
        if (strcmp(actual->palabra, buf) == 0) {
            int coincidencias = 0;
            for (int i = 0; i < actual->num_indices; i++) {
                int idx = actual->indices_tipo[i];
                if (idx >= 0 && idx < num_tipos) {
                    frecuencias[idx]++;
                    coincidencias++;
                }
            }
            return coincidencias;
        }
        actual = actual->siguiente;
    }
    return 0;
}

void liberarConfig(ConfigIALearner *config) {
    if (config->tabla_hash) {
        for (int i = 0; i < config->tam_hash; i++) {
            EntradaHash *actual = config->tabla_hash[i];
            while (actual) {
                EntradaHash *siguiente = actual->siguiente;
                free(actual->palabra);
                free(actual->indices_tipo);
                free(actual);
                actual = siguiente;
            }
        }
        free(config->tabla_hash);
        config->tabla_hash = NULL;
    }

    free(config->tipos);
    config->tipos = NULL;

    if (config->reglas) {
        for (int i = 0; i < config->num_reglas; i++) {
            free(config->reglas[i].condiciones);
        }
        free(config->reglas);
        config->reglas = NULL;
    }
}

void imprimirConfig(const ConfigIALearner *config) {
    printf("────────────────── Configuracion cargada ──────────────────\n");
    printf("Tipos de documento: %d\n", config->num_tipos);
    for (int i = 0; i < config->num_tipos; i++) {
        printf("  [%d] %s (%d palabras)\n", i, config->tipos[i].nombre, config->tipos[i].num_palabras);
    }
    printf("Tabla hash: %d cubetas\n", config->tam_hash);
    printf("Delimitadores: \"%s\"\n", config->delimitadores);
    printf("Umbral ocurrencias: %d\n", config->umbral_ocurrencias);
    printf("Reglas de usuario: %d\n", config->num_reglas);
    for (int i = 0; i < config->num_reglas; i++) {
        printf("  %s:\n", config->reglas[i].nombre);
        for (int j = 0; j < config->reglas[i].num_condiciones; j++) {
            int idx = config->reglas[i].condiciones[j].indice_tipo;
            printf("    %s >= %.0f%%\n", config->tipos[idx].nombre, config->reglas[i].condiciones[j].proporcion_min * 100);
        }
    }
    printf("───────────────────────────────────────────────────────────\n");
}