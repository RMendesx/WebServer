#include <stdlib.h>          // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)
#include <stdio.h>           // Biblioteca padrão para entrada e saída
#include <string.h>          // Biblioteca manipular strings
#include "pico/stdlib.h"     // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"    // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "hardware/pwm.h"    // Biblioteca para controle de PWM (modulação por largura de pulso)
#include "pico/cyw43_arch.h" // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "lwip/pbuf.h"       // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"        // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"      // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)
#include "lib/buzzer.h"      // Biblioteca para controle de buzzer
#include "lib/ssd1306.h"
#include "lib/font.h"

#define WIFI_SSID "XXX"
#define WIFI_PASSWORD "XXXXXXXXXXXXX"

#define BUTTON_PIN 5                  // GPIO5 - Botão
#define LED_PIN CYW43_WL_GPIO_LED_PIN // GPIO do CI CYW43
#define LED_GREEN_PIN 11              // GPIO11 - LED verde
#define LED_RED_PIN 13                // GPIO13 - LED vermelho
#define BUZZER1_PIN 10                // GPIO10 - Buzzer 1
#define BUZZER2_PIN 21                // GPIO21 - Buzzer 2
#define I2C_PORT i2c1                 // I2C1
#define I2C_SDA 14                    // GPIO14 - SDA
#define I2C_SCL 15                    // GPIO15 - SCL
#define endereco 0x3C                 // Endereço do display OLED

bool alarm_active = false;

/*--------------------------------------------------------------------------------------------------------------------------------------------*/

// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

/*--------------------------------------------------------------------------------------------------------------------------------------------*/

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void);

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_button_bitdog(void);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Tratamento do request do usuário
void user_request(char **request);

// Função para iniciar o alarme
void start_alarm();

// Função para parar o alarme
void stop_alarm();

/*--------------------------------------------------------------------------------------------------------------------------------------------*/

int main()
{
    // Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    gpio_led_bitdog();

    // Inicializar os Pinos GPIO para acionamento dos Botões da BitDogLab
    gpio_button_bitdog();

    // Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    // vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    /*--------------------------------------------------------------------------------------------------------------------------------------------*/

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA);                                        // Pull up the data line
    gpio_pull_up(I2C_SCL);                                        // Pull up the clock line
    ssd1306_t ssd;                                                // Inicializa a estrutura do display
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd);                                         // Configura o display
    ssd1306_send_data(&ssd);                                      // Envia os dados para o display

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    bool cor = true;

    /*--------------------------------------------------------------------------------------------------------------------------------------------*/

    ssd1306_fill(&ssd, !cor);                      // Limpa o display
    ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);  // Desenha um retângulo
    ssd1306_draw_string(&ssd, "SEGURANCA", 29, 6); // Desenha uma string
    ssd1306_line(&ssd, 3, 15, 123, 15, cor);       // Desenha uma linha
    ssd1306_send_data(&ssd);                       // Atualiza o display

    while (true)
    {
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo

        while (alarm_active)
        {

            ssd1306_draw_string(&ssd, "ATIVADO.  ", 26, 36); // Desenha uma string
            ssd1306_send_data(&ssd);
            buzzer_on(BUZZER1_PIN, 2000);
            buzzer_on(BUZZER2_PIN, 2000);
            sleep_ms(200);

            ssd1306_draw_string(&ssd, "ATIVADO.. ", 26, 36); // Desenha uma string
            ssd1306_send_data(&ssd);
            buzzer_off(BUZZER1_PIN);
            buzzer_off(BUZZER2_PIN);
            sleep_ms(100);

            ssd1306_draw_string(&ssd, "ATIVADO...", 26, 36); // Desenha uma string
            ssd1306_send_data(&ssd);
            sleep_ms(100);
        }

        ssd1306_draw_string(&ssd, "          ", 26, 36); // Desenha uma string
        ssd1306_send_data(&ssd);
        buzzer_off(BUZZER1_PIN);
        buzzer_off(BUZZER2_PIN);
        sleep_ms(100); // Reduz o uso da CPU
    }

    // Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

/*--------------------------------------------------------------------------------------------------------------------------------------------*/

void gpio_button_bitdog(void)
{
    // Configuração do botão A
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    // Configuração do botão B
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
}

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void)
{
    // Configuração dos LEDs como saída
    gpio_init(LED_GREEN_PIN);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    gpio_put(LED_GREEN_PIN, false);

    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, false);
}

void start_alarm()
{
    // Evita reentrada se o alarme já estiver ativo
    if (alarm_active)
        return;

    gpio_put(LED_RED_PIN, 0);
    gpio_put(LED_GREEN_PIN, 1);

    // Espera o botão A (GPIO 5) ser pressionado
    printf("Aguardando pressionamento do botão A para ativar o buzzer...\n");
    while (gpio_get(BUTTON_PIN))
    {
        sleep_ms(10); // Verifica a cada 10 ms
    }

    // Quando o botão for pressionado, toca o alarme
    printf("Botão precionado, tocando alarme!\n");
    alarm_active = true;
}

void stop_alarm()
{
    alarm_active = false;
    buzzer_off(BUZZER1_PIN);
    buzzer_off(BUZZER2_PIN);
    gpio_put(LED_RED_PIN, 1);
    gpio_put(LED_GREEN_PIN, 0);
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request)
{

    if (strstr(*request, "GET /alarm_on") != NULL)
    {
        start_alarm();
    }
    else if (strstr(*request, "GET /alarm_off") != NULL)
    {
        stop_alarm();
    }
};

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);

    // Cria a resposta HTML
    char html[1024];

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title> Embarcatech - LED Control </title>\n"
             "<style>\n"
             "body { background-color: #b5e5fb; font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
             "h1 { font-size: 64px; margin-bottom: 30px; }\n"
             "button { background-color: LightGray; font-size: 36px; margin: 10px; padding: 20px 40px; border-radius: 10px; }\n"
             ".temperature { font-size: 48px; margin-top: 30px; color: #333; }\n"
             "</style>\n"
             "</head>\n"
             "<body>\n"
             "<h1>Sistema de Alarme</h1>\n"
             "<form action=\"./alarm_on\"><button>Ativar Alarme</button></form>\n"
             "<form action=\"./alarm_off\"><button>Desativar Alarme</button></form>\n"
             "</body>\n"
             "</html>\n");

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    // libera memória alocada dinamicamente
    free(request);

    // libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}