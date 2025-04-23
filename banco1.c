#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include "config.h"

#define CUENTAS "cuentas.dat"

#define BUFFER_SIZE 1024 // tamaño para la comunicacion entre procesos (anomalias)

#define MAX_HILOS 100

int numHilos = 0; // contador hilos activos
int contadorUsuarios = 0; // contador de usuarios en el sistema

pthread_t hilos[MAX_HILOS]; // array de hilos
pthread_mutex_t mutex_contador = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_log_gen = PTHREAD_MUTEX_INITIALIZER;

// Estructura de la cuenta bancaria
typedef struct
{
    int numero_cuenta;
    char titular[100];
    float saldo;
    int pin;
    int num_transacciones;
} CuentaBancaria;

sem_t semaforo;
Config configuracion_sys;


void registro_log_general(const char *tipo, const char *descripcion)
{
    // bloqueo del mutex
    pthread_mutex_lock(&mutex_log_gen);

    FILE *log_gen = fopen("application.log", "a");
    if (!log_gen)
    {
        perror("Error al abrir application.log");
        pthread_mutex_unlock(&mutex_log_gen);
        registro_log_general("Main", "Error al abrir application.log");
        return;
    }

    // obtener fecha y hora atual
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    // estructura y escritura del registro
    fprintf(log_gen, "[%s] | Operación: %s | Descripcion: %s\n",
            fecha_hora, tipo, descripcion);

    fclose(log_gen);
    pthread_mutex_unlock(&mutex_log_gen);
}

void init_banco()
{
    sem_init(&semaforo, 1, 1); // 1 = compartido entre procesos e inicializado en 1
    printf("=== Banco inciado ===\n");
    registro_log_general("Main", "Banco iniciado");
}

void print_banner()
{

    printf(" /$$$$$$                                                    /$$$$$$$                      /$$ \n");
    printf(" /$$__  $$                                                  | $$__  $$                    | $$      \n");
    printf("| $$  \\__/  /$$$$$$   /$$$$$$$ /$$   /$$  /$$$$$$   /$$$$$$ | $$  \\ $$  /$$$$$$  /$$$$$$$ | $$   /$$\n");
    printf("|  $$$$$$  /$$__  $$ /$$_____/| $$  | $$ /$$__  $$ /$$__  $$| $$$$$$$  |____  $$| $$__  $$| $$  /$$/\n");
    printf(" \\____  $$| $$$$$$$$| $$      | $$  | $$| $$  \\__/| $$$$$$$$| $$__  $$  /$$$$$$$| $$  \\ $$| $$$$$$/ \n");
    printf(" /$$  \\ $$| $$_____/| $$      | $$  | $$| $$      | $$_____/| $$  \\ $$ /$$__  $$| $$  | $$| $$_  $$ \n");
    printf("|  $$$$$$/|  $$$$$$$|  $$$$$$$|  $$$$$$/| $$      |  $$$$$$$| $$$$$$$/|  $$$$$$$| $$  | $$| $$ \\  $$\n");
    printf(" \\______/  \\_______/ \\_______/ \\______/ |__/       \\_______/|_______/  \\_______/|__/  |__/|__/  \\__/\n");
    // printf(" ---------------------------------------------------------------- \n\n");
}

void comprobacion_anomalias() {
    int pipefd[2]; // tuberia
    pid_t pid;
    char buffer[BUFFER_SIZE];

    if (pipe(pipefd) == -1) { // creacion de la tuberia
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid = fork(); // creacion de proceso hijo
    if (pid == -1) { 
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) { // Proceso hijo (monitor)
        close(pipefd[0]);  // Cerrar extremo de lectura
        dup2(pipefd[1], STDERR_FILENO);  // Redirigir stderr a la tubería
        close(pipefd[1]);
        
        execl("./monitor", "monitor", NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else { // Proceso padre (banco)
        close(pipefd[1]);  // Cerrar extremo de escritura
        
        // Configurar el descriptor como no bloqueante
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        close(pipefd[0]);
        // espera del hijo
        wait(NULL);
    }
}
// ===============================
void *abrir_terminal(void *arg)
{
    // controlar el maximo de usuarios
    pthread_mutex_lock(&mutex_contador);
    if (contadorUsuarios >= configuracion_sys.num_hilos)
    {
        printf("Limite de usuarios alcanzado\n");
        pthread_mutex_unlock(&mutex_contador);
        pthread_exit(NULL);
        registro_log_general("Main", "Limite de usuarios alcanzado");
    }
    contadorUsuarios++;
    pthread_mutex_unlock(&mutex_contador);

    printf("Abriendo terminal. Usuarios activos: %d/%d\n", contadorUsuarios, configuracion_sys.num_hilos);
    registro_log_general("Main", "Abriendo terminal");

    // ejecucion de la terminal
    int status = system("x-terminal-emulator -e ./usuario");

    // al cerrar se decrementa el contador
    pthread_mutex_lock(&mutex_contador);
    contadorUsuarios--;
    pthread_mutex_unlock(&mutex_contador);

    pthread_exit(NULL);
}

void *bucle_menu(void *arg)
{
    init_banco();
    print_banner();

    // cargar la configuracion
    configuracion_sys = leer_configuracion("config.txt");
    // Verificación de valores
    if (configuracion_sys.num_hilos <= 0)
    {
        fprintf(stderr, "Error: NUM_HILOS debe ser positivo (Valor leído: %d)\n",
                configuracion_sys.num_hilos);
        registro_log_general("Main", "Error num hilos debe ser positivo");
        exit(EXIT_FAILURE);
    }

    // ponemos un contador para saber cuantos usuarios tenemos a la vez abiertos
    int numHijos = 0;

    // ponemos a ejecutase monitor para la comprobacion de anomalias
    pid_t pid = fork();
    //

    if (pid == 0)
    {
        // para abrir monitor y que este comprobando todo el rato las anomalias
        comprobacion_anomalias();
    }
    
    // Bucle del menu principal
    while (1)
    {
        sleep(2);
        int opcion = 0;

        printf("Actualmente hay %d/%d usuarios abiertos.\n", contadorUsuarios, configuracion_sys.num_hilos);
        printf("1.Acceder al sistema\n");
        printf("2.Cerrar\n");
        scanf("%d", &opcion);

        switch (opcion)
        {
        case 1: 
            // Abrir terminal
            if (contadorUsuarios < configuracion_sys.num_hilos)
            {
                pthread_t thread;
                if (pthread_create(&thread, NULL, abrir_terminal, NULL) != 0)
                {
                    perror("Error al crear hijo");
                    registro_log_general("Main", "Error al abrir terminal");
                }
                else
                {
                    hilos[numHilos++] = thread;
                    // contadorUsuario++;
                }
            }
            else
            {
                printf("Se ha alcanzado el numero de usuarios maximo (%d)", configuracion_sys.num_hilos);
                registro_log_general("Main", "Numero de usuarios max. alcanzado");
            }
            sleep(2); // Esperar un poco entre iteraciones
            break;

        case 2:
            // Cerrar todo el sistema
            printf("Cerrando todos los terminales....\n");
            registro_log_general("Main", "Cerrando terminales");
            sleep(2);
            int cerrar_usuario = system("killall ./usuario"); // Usa system()
            int cerrar_monitor = system("killall ./monitor");
            int cerrar_banco = system("killall ./banco");
            printf("Saliendo.......\n");
            registro_log_general("Main", "Salida del sistema");
            exit(0);
            break;

        default:
            printf("Introduzca un valor valido.\n");
            registro_log_general("Main", "Error de usuario opcion del menu");
            break;
        }   
    }
}

int main()
{
    // hilo para el menu principal
    pthread_t hilo_bucle;
    if (pthread_create(&hilo_bucle, NULL, bucle_menu, NULL) != 0)
    {
        perror("Error al crear el hilo principal");
        registro_log_general("Main", "Error al crear el hilo principal");
        return 1;
    }

    // Esperamos a que el hilo del bucle termine (lo mantiene activo)
    pthread_join(hilo_bucle, NULL);

    return 0;
}
