# Agentic OS Backdoor

Sistema compuesto por dos programas:

- **`ialearner`**: proceso receptor/clasificador. Recibe teclas desde las ventanas gráficas, aplica Bag of Words para clasificar documentos e infiere el tipo de usuario.
- **`launcher`**: proceso que crea y monitorea ventanas gráficas (X11), y se comunica con `ialearner` vía sockets TCP.

## Archivos del proyecto

```
ialearner.c          Lógica del servidor/clasificador
config.c / config.h  Parseo de archivos de configuración (diccionarios y reglas)
launcher.c           Proceso launcher (ventanas X11)
protocoloComms.h      Protocolo de mensajes compartido entre launcher e ialearner
```

## Requisitos

- `gcc`
- Biblioteca **pthreads** (incluida en la mayoría de sistemas Linux)
- Biblioteca de desarrollo de **X11** (necesaria solo para `launcher`)


## Compilacion manualmente con gcc

Si no se quiere usar el Makefile, los comandos equivalentes son:

### Compilar `ialearner`

`ialearner` está compuesto por `ialearner.c` y `config.c`, y usa hilos (pthreads):

```bash
gcc -Wall -Wextra -g -o ialearner ialearner.c config.c -pthread
```

### Compilar `launcher`

`launcher` usa hilos y la biblioteca gráfica X11:

```bash
gcc -Wall -Wextra -g -o launcher launcher.c -pthread -lX11
```


## Ejecución

### 1. Iniciar `ialearner`

```bash
./ialearner <puerto> <archivo_conf_diccionarios> <archivo_conf_reglas>
```

Ejemplo:

```bash
./ialearner 5000 diccionarios.conf reglas.conf
```

### 2. Iniciar uno o más `launcher`

Cada `launcher` necesita un archivo de configuración con el host y puerto de `ialearner`:

```
IALEARNER_HOST 127.0.0.1
IALEARNER_PORT 5000
```

Luego se ejecuta con:

```bash
./launcher launcher.conf
```

Dentro del launcher, usar el comando `ayuda` para ver los comandos disponibles (`crear`, `estado`, `cerrar`, `salir`, etc.).

## Compilacion con el Makefile incluido (recomendado)

Con el `Makefile` incluido en el proyecto, basta con ejecutar:

```bash
make
```

Esto genera ambos ejecutables: `ialearner` y `launcher`.

También se pueden compilar por separado:

```bash
make ialearner
make launcher
```

Para limpiar los binarios y archivos objeto generados:

```bash
make clean
```

## Notas

- `launcher` requiere un entorno gráfico X11 activo (variable `DISPLAY` configurada) para poder crear ventanas.
- Ambos procesos pueden ejecutarse en la misma máquina o en máquinas distintas dentro de la misma red, siempre que `IALEARNER_HOST`/`IALEARNER_PORT` apunten correctamente al proceso `ialearner`.