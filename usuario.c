#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include <sys/ipc.h> // Libreria parte 2

#include "config.h"

#define CUENTAS "cuentas.dat"

// Estructura de la cuenta bancaria
struct Cuenta
{
    int numero_cuenta;
    char titular[100];
    float saldo;
    int pin;
    int num_transacciones;
};


// Estructura para manejar la transferencia con hilos
struct TransferData
{
    struct Cuenta *cuenta;
    int num_cuenta_destino;
    float cantidad;
    Config *config;
};
  
// Declaraciones de funciones
void *DepositarDinero(void *arg);
void *RetirarDinero(void *arg);
void *Transferencia(void *arg);
void *ConsultarSaldo(void *arg);
int login();

void print_banner();
struct Cuenta buscar_cuenta(int numero_cuenta);
int buscar_cuenta_log(int num_cuenta, int pin);
void actualizar_cuenta(struct Cuenta *cuenta);
void registrar_transaccion(const char *tipo, int numero_cuenta, float monto, float saldo_final);
int inicio_sesion();
void registro_log_general(const char *tipo, int numero_cuenta, const char *descripcion);

// Variables globales para sincronización
sem_t semaforo;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // op criticas
pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER; // Mutex para log de transacciones
pthread_mutex_t mutex_log_gen = PTHREAD_MUTEX_INITIALIZER; // Mutex para application.log

Config configuracion_sys; 

int main()
{
    // Cargar el config del sistema
    configuracion_sys  = leer_configuracion("config.txt");

    //ACcede al segmento compartido


    int cuenta_id = 0;

    print_banner();

    // Autenticacion de usuario
    cuenta_id = inicio_sesion();
    system("clear"); // borra todo lo que hubiera arriba

    // Obtener los datos de la cuenta 
    struct Cuenta cuentaUsuario = buscar_cuenta(cuenta_id);

    int opcion = 0;

    // Menu principal
    while (opcion != 5)
    {
        print_banner();
        printf("¿Qué quieres hacer en tu cuenta?\n");
        printf("1. Depositar dinero \n");
        printf("2. Retirar dinero \n");
        printf("3. Hacer transferencia \n");
        printf("4. Consultar saldo \n");
        printf("5. Salir \n");
        scanf("%d", &opcion);

        pthread_t hilo;
        switch (opcion)
        {
        case 1:
            // Depositar Dinero
            pthread_create(&hilo, NULL, DepositarDinero, &cuentaUsuario);
            break;
        case 2:
            // Retirar Dinero
            pthread_create(&hilo, NULL, RetirarDinero, &cuentaUsuario);
            break;
        case 3:
            // Transferencia
            // Datos para la transferencia
            struct TransferData *data = malloc(sizeof(struct TransferData));
            data->cuenta = &cuentaUsuario;
            data->config = &configuracion_sys;

            printf("Introduzca la cuenta destino: ");
            scanf("%d", &data->num_cuenta_destino);
            printf("Ingrese la cantidad a transferir: ");
            scanf("%f", &data->cantidad);

            pthread_create(&hilo, NULL, Transferencia, data);
            break;
        case 4:
            // Consultar Saldo
            pthread_create(&hilo, NULL, ConsultarSaldo, &cuentaUsuario);
            break;
        case 5:
            //Salir
            printf("Saliendo.......\n");
            break;
        default:
            system("clear");
            printf("Introduzca una opción válida por favor\n");
            break;
        };
        
        pthread_join(hilo, NULL); // Esperar a que el hilo termine
        system("clear"); // Limpiar pantalla
    };

    // Limpieza de recursos
    sem_destroy(&semaforo);
    return 0;
};

// Inicio de sesion
int inicio_sesion(){
    sem_init(&semaforo, 1, 1); // Inicializacion de semaforo
    int opcion1 = 0;
    int cuenta_id = 0;

    // Menu Inicio de sesion
    while(1){
        printf("1. Iniciar sesion\n");
        printf("2. Salir\n");
        scanf("%d", &opcion1);

        switch(opcion1){
            case 1:
            // Inciar sesion 
            cuenta_id = login();
            if (cuenta_id == -1)
            {
                printf("Cerrando aplicacion.....\n");
                exit(1);
            }
            return cuenta_id;
            break;

            case 2:
                // Salir
                exit(1);
                break;
            default:
                printf("Introduzca un valor valido.\n");
            break;
        }
    }
}

// Autenticacion de usuario
int login()
{
    int numero_cuenta = 0, intentos = 3;
    int pin;

    while (intentos > 0)
    {
        printf("Ingrese el número de cuenta: \n");
        scanf("%d", &numero_cuenta);
        printf("Ingrese el PIN de la cuenta:\n");
        scanf("%d", &pin);

        int comprobacion = buscar_cuenta_log(numero_cuenta, pin);

        if (comprobacion == 1)
        {
            printf("Cuenta encontrada. ¡Bienvenido!\n");
            registro_log_general("Login", numero_cuenta, "Login exitoso");
            return numero_cuenta;
        }
        else
        {
            intentos--;
            printf("La cuenta no fue encontrada. Intentos restantes: %d\n", intentos);
            registro_log_general("Login", numero_cuenta, "Cuenta no encontrada");
        }
    }

    printf("Demasiados intentos. Vuelve más tarde.\n");
    registro_log_general("Login", numero_cuenta, "Demasiados intentos de login");
    return -1;
}

// Registro de transacciones en transacciones.log
void registrar_transaccion(const char *tipo, int numero_cuenta, float monto, float saldo_final)
{
    pthread_mutex_lock(&mutex_log);

    FILE *log = fopen("transacciones.log", "a");
    if (!log)
    {
        perror("Error al abrir transacciones.log");
        pthread_mutex_unlock(&mutex_log);
        return;
    }

    // Obtener fecha y hora actual
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    // Estructura y escritura que se registra en el log
    fprintf(log, "[%s] Cuenta: %d | Operación: %s | Monto: %.2f | Saldo final: %.2f\n",
            fecha_hora, numero_cuenta, tipo, monto, saldo_final);

    fclose(log);
    pthread_mutex_unlock(&mutex_log);
}

// Registro de eventos generales del sistema en application.log
void registro_log_general(const char *tipo, int numero_cuenta, const char *descripcion){
    pthread_mutex_lock(&mutex_log_gen);

    FILE *log_gen = fopen("application.log", "a");
    if (!log_gen)
    {
        perror("Error al abrir application.log");
        pthread_mutex_unlock(&mutex_log_gen);
        return;
    }

    // obtener fecha y hora actual
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    // Estructura y escritura que se registra en el log
    fprintf(log_gen, "[%s] Cuenta: %d | Operación: %s | Descripcion: %s\n",
            fecha_hora, numero_cuenta, tipo, descripcion );

    fclose(log_gen);
    pthread_mutex_unlock(&mutex_log_gen);
}

// Función para actualizar el saldo en cuentas.dat
void actualizar_cuenta(struct Cuenta *cuenta)
{
    sem_wait(&semaforo);
    FILE *archivo = fopen(CUENTAS, "rb+");
    struct Cuenta temp;

    // buscar la cuenta y actualizar
    while (fread(&temp, sizeof(struct Cuenta), 1, archivo))
    {
        if (temp.numero_cuenta == cuenta->numero_cuenta)
        {
            fseek(archivo, -sizeof(struct Cuenta), SEEK_CUR);
            fwrite(cuenta, sizeof(struct Cuenta), 1, archivo);
            fflush(archivo);
            break;
        }
    }

    fclose(archivo);
    // libera semaforo
    sem_post(&semaforo); 
}

// Función para retirar dinero
void *RetirarDinero(void *arg)
{
    struct Cuenta *cuenta = (struct Cuenta *)arg;
    float cantidad_retirar;

    printf("¿Cuánto dinero quiere retirar?\n");
    printf("Solo puede retirar un monto maximo de: (%d)\n", configuracion_sys.limite_retiro);
    scanf("%f", &cantidad_retirar);

    pthread_mutex_lock(&mutex);
    if (cantidad_retirar > cuenta->saldo)
    {
        printf("Fondos insuficientes.\n");
        registro_log_general("Retiro", cuenta->numero_cuenta, "Retiro rechazado por fondos insuficientes");

    } 
    else if(cantidad_retirar > configuracion_sys.limite_retiro){
        printf("El monto excede el limite para retiros (%d)\n", configuracion_sys.limite_retiro);
        registro_log_general("Retiro", cuenta->numero_cuenta, "Retiro rechazado por exceder limite");
    }
   
    else
    {
        // Realiza el retiro
        cuenta->saldo -= cantidad_retirar;
        cuenta->num_transacciones++;
        actualizar_cuenta(cuenta);
        registrar_transaccion("Retiro", cuenta->numero_cuenta, cantidad_retirar, cuenta->saldo);
        registro_log_general("Retiro", cuenta->numero_cuenta, "Usuario ha realizado un retiro");
        printf("Retiro realizado. Nuevo saldo: %.2f\n", cuenta->saldo);
    }
    pthread_mutex_unlock(&mutex);
    sleep(5);

    return NULL;
}

// Función para depositar dinero
void *DepositarDinero(void *arg)
{
    struct Cuenta *cuenta = (struct Cuenta *)arg;
    float cantidad_depositar;

    printf("¿Cuánto dinero quiere depositar?\n");
    scanf("%f", &cantidad_depositar);

    pthread_mutex_lock(&mutex);

    // Realiza el deposito
    cuenta->saldo += cantidad_depositar;
    cuenta->num_transacciones++;
    actualizar_cuenta(cuenta);
    pthread_mutex_unlock(&mutex);

    // Registros
    registrar_transaccion("Depósito", cuenta->numero_cuenta, cantidad_depositar, cuenta->saldo);
    registro_log_general("Depósito", cuenta->numero_cuenta, "Usuario ha realizado un depósito");
    printf("Depósito realizado. Nuevo saldo: %.2f\n", cuenta->saldo);
    sleep(5);
    return NULL;
}

// Transferencia de dinero
void *Transferencia(void *arg)
{
    struct TransferData *data = (struct TransferData *)arg;
    struct Cuenta *cuenta_origen = data->cuenta;
    Config *config = data->config;

    int num_cuenta_destino = data->num_cuenta_destino;
    float cantidad = data->cantidad;

    
    // Buscar la cuenta destino
    struct Cuenta cuenta_destino = buscar_cuenta(data->num_cuenta_destino);

    pthread_mutex_lock(&mutex);

    // Verificar fondos suficientes en la cuenta origen
    if (cantidad > cuenta_origen->saldo)
    {
        printf("Fondos insuficientes para la transferencia.\n");
        registro_log_general("Transferencia fallida", cuenta_origen->numero_cuenta, "Rechazada por fondos insuficientes");
        pthread_mutex_unlock(&mutex);
        sleep(5);
        return NULL;
    }

    // Verificar la cantidad que se va a transferir con el limite para la cantidad
    if (data->cantidad > config->limite_tranferencia){
        printf("El monto excede el limite para transferencias (%d)\n", config->limite_tranferencia);
        registro_log_general("Transferencia fallida", cuenta_origen->numero_cuenta, "Rechazada tras exceder limite");
        pthread_mutex_unlock(&mutex);
        sleep(5);
        return NULL;
    }

    // Realizar la transferencia
    cuenta_origen->saldo -= cantidad;
    cuenta_destino.saldo += cantidad;

    // Actualizar ambas cuentas en el archivo
    actualizar_cuenta(cuenta_origen);
    actualizar_cuenta(&cuenta_destino);

    // Registrar la transacción
    registrar_transaccion("Transferencia realizada", cuenta_origen->numero_cuenta, cantidad, cuenta_origen->saldo);
    registrar_transaccion("Transferencia recibida", cuenta_destino.numero_cuenta, cantidad, cuenta_destino.saldo);
    registro_log_general("Transferencia realizada", cuenta_origen->numero_cuenta, "Transferencia realizada por usuario");
    registro_log_general("Transferencia recibida", cuenta_destino.numero_cuenta, "Transferencia recibida por usuario");


    printf("Transferencia realizada. Nuevo saldo: %.2f\n", cuenta_origen->saldo);
    sleep(5);

    pthread_mutex_unlock(&mutex);
    return NULL;
}

void *ConsultarSaldo(void *arg) {
    struct Cuenta *cuenta_local = (struct Cuenta *)arg;
    
    // Bloquear el mutex para evitar condiciones de carrera
    pthread_mutex_lock(&mutex);
    
    // Buscar la cuenta actualizada en el archivo
    struct Cuenta cuenta_actualizada = buscar_cuenta(cuenta_local->numero_cuenta);
    
    // Mostrar la información actualizada
    printf("Titular: %s\n", cuenta_actualizada.titular);
    printf("Número de cuenta: %d\n", cuenta_actualizada.numero_cuenta);
    printf("Saldo actual: %.2f\n", cuenta_actualizada.saldo);
    printf("Transacciones realizadas: %d\n", cuenta_actualizada.num_transacciones);
    
    printf("========================================\n");
    
    
    pthread_mutex_unlock(&mutex);
    
    sleep(5); // Tiempo para que el usuario pueda leer la información
    return NULL;
}

// Buscar una cuenta en el archivo
struct Cuenta buscar_cuenta(int numero_cuenta)
{
    sem_wait(&semaforo);
    FILE *archivo = fopen(CUENTAS, "rb");

    // Definimos una cuenta vacia 
    struct Cuenta cuenta_aux = {-1, "", 0.0, 0, 0};

    if (!archivo)
    {
        perror("Error al abrir cuentas.dat");
        registro_log_general("Busqueda_cuenta", numero_cuenta, "Busqueda fallida, archivo de cuentas inexistente");
        sem_post(&semaforo);
        return cuenta_aux;
    }

    // busqueda de cuenta en el fichero y registro
    while (fread(&cuenta_aux, sizeof(struct Cuenta), 1, archivo))
    {
        if (cuenta_aux.numero_cuenta == numero_cuenta)
        {
            fclose(archivo);
            registro_log_general("buscar_cuenta", numero_cuenta, "Cuenta encontrada");
            sem_post(&semaforo);
            return cuenta_aux;
        }
    }

    fclose(archivo);
    sem_post(&semaforo);
    return cuenta_aux;
}

// Busqueda de cuenta con autenticacion
int buscar_cuenta_log(int num_cuenta, int pin)
{
    FILE *archivo = fopen(CUENTAS, "rb"); // Abrimos en modo binario de lectura
    struct Cuenta cuenta_aux;

    if (!archivo)
    {
        perror("Error al abrir el archivo cuentas.dat");
        registro_log_general("busqueda_cuenta_log", num_cuenta, "Error al abrir el archivo cuentas.dat");
        sem_post(&semaforo);

        return -1; 
    }

    // buscar una cuenta que coincida con el numero y contraseña que se han introducido
    while (fread(&cuenta_aux, sizeof(struct Cuenta), 1, archivo))
    {
        if (cuenta_aux.numero_cuenta == num_cuenta && cuenta_aux.pin == pin)
        {
            fclose(archivo);
            printf("Cuenta encontrada");
            registro_log_general("buscar_cuenta_log", num_cuenta, "Cuenta encontrada");
            return 1; // Devolvemos la cuenta encontrada
        }
    }

    fclose(archivo);

    struct Cuenta cuenta_vacia = {-1, "", 0.0, 0, 0}; // Devolver cuenta vacía si no se encuentra
    return -1;
}
void print_banner(){
    printf("$$\\      $$\\ $$$$$$$$\\ $$\\   $$\\ $$\\   $$\\       $$\\   $$\\  $$$$$$\\  $$\\   $$\\  $$$$$$\\  $$$$$$$\\  $$$$$$\\  $$$$$$\\  \n");
    printf("$$$\\    $$$ |$$  _____|$$$\\  $$ |$$ |  $$ |      $$ |  $$ |$$  __$$\\ $$ |  $$ |$$  __$$\\ $$  __$$\\ \\_$$  _|$$  __$$\\ \n");
    printf("$$$$\\  $$$$ |$$ |      $$$$\\ $$ |$$ |  $$ |      $$ |  $$ |$$ /  \\__|$$ |  $$ |$$ /  $$ |$$ |  $$ |  $$ |  $$ /  $$ |\n");
    printf("$$\\$$\\$$ $$ |$$$$$\\    $$ $$\\$$ |$$ |  $$ |      $$ |  $$ |\\$$$$$$\\  $$ |  $$ |$$$$$$$$ |$$$$$$$  |  $$ |  $$ |  $$ |\n");
    printf("$$ \\$$$  $$ |$$  __|   $$ \\$$$$ |$$ |  $$ |      $$ |  $$ | \\____$$\\ $$ |  $$ |$$  __$$ |$$  __$$<   $$ |  $$ |  $$ |\n");
    printf("$$ |\\$  /$$ |$$ |      $$ |\\$$$ |$$ |  $$ |      $$ |  $$ |$$\\   $$ |$$ |  $$ |$$ |  $$ |$$ |  $$ |  $$ |  $$ |  $$ |\n");
    printf("$$ | \\_/ $$ |$$$$$$$$\\ $$ | \\$$ |\\$$$$$$  |      \\$$$$$$  |\\$$$$$$  |\\$$$$$$  |$$ |  $$ |$$ |  $$ |$$$$$$\\  $$$$$$  |\n");
    printf("\\__|     \\__|\\________|\\__|  \\__| \\______/        \\______/  \\______/  \\______/ \\__|  \\__|\\__|  \\__|\\______| \\______/ \n");
    
}
