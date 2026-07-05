/* config.h - Estructuras y funciones de configuracion para IALearner 
 Autor: Josue Sanchez C.
*/

#ifndef CONFIG_H
#define CONFIG_H

/* Una entrada en la tabla hash, osea una palabra (usa chaining para colisiones) */
typedef struct EntradaHash {
    char *palabra;
    int indice_tipo; // TipoDocumento
    struct EntradaHash *siguiente; // Siguiente si hubo colision
} EntradaHash;

/* Un tipo de documento (CORREO, ARTICULO, REPORTE, o los que defina el servidor) */
typedef struct {
    char nombre[64]; // ej: "CORREO"
    int num_palabras; // total de palabras de todos los diccionarios de este tipo
} TipoDocumento;

/* Una regla de proporcion mínima de un TipoDocumento para un usuario */
typedef struct {
    int indice_tipo; // TipoDoc, indice en ConfigIALearner.tipos[]
    double proporcion_min; // proporcion minima requerida para aportar (0.0 a 1.0)
} CondicionUsuario;

/* Todas las reglas de proporciones minimas de TiposDocumentos que definen un tipo de usuario */
typedef struct {
    char nombre[64]; // ej: "Profesor"
    CondicionUsuario *condiciones;
    int num_condiciones;
} ReglaUsuario;

/* Configuracion global, se llena al parsear los archivos de configuracion al ejecutar ialearner */
typedef struct {
    TipoDocumento *tipos; // arreglo dinamico para guardar los tipos de documento
    int num_tipos;
    int cap_tipos;

    EntradaHash **tabla_hash; // array de cubetas, la tabla hash, tam dinamico
    int tam_hash; // numero de cubetas, entradas de la tabla (primo)

    ReglaUsuario *reglas; // arreglo dinamico de las reglas de usuario
    int num_reglas;
    int cap_reglas;

    char delimitadores[32];
    int umbral_ocurrencias; // palabras minimas qeu deben pertenecer al menos a 1 diccionario
} ConfigIALearner;


/* Parsea el archivo de configuracion de los diccionarios (diccionarios.conf),
 llena tipos[] y tabla_hash. Retorna 0 en exito, -1 en error. */
int parsearDiccionarios(const char *ruta, ConfigIALearner *config);

/* Parsea el archivo de configuracion de los tipos de usuarios y reglas (reglas.conf),
 llena reglas[], delimitadores y umbral. Retorna 0 en exito, -1 en error. */
int parsearReglas(const char *ruta, ConfigIALearner *config);

/* Busca el tipo de Documento de una palabra en la tabla hash (es case insensitive).
 Retorna el indice_tipo si existe, -1 si no esta en ningun diccionario. */
int indiceTipoPalabraEnHash(const char *palabra, ConfigIALearner *config);

/* Libera toda la memoria dinamica de la configuracion */
void liberarConfig(ConfigIALearner *config);

/* Imprime la configuracion cargada (para debug/verificacion al arrancar) */
void imprimirConfig(const ConfigIALearner *config);

#endif