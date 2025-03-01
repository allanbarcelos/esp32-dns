# ESP32 Dynamic DNS with MQTT and Bluetooth Keyboard

 Português | [English](README.md)

- Este projeto foi feito ha alguns anos, o repositorio original encontra-se privado por conter dados sensiveis, estou disponibilizando o mesmo codigo sem incluir dados sensiveis mas com com algumas mudanças e melhorias (ainda nao testadas) 

Este projeto utiliza um ESP32 para atualizar dinamicamente registros DNS no Cloudflare, baseado no IP público do dispositivo. Além disso, o ESP32 também pode responder a comandos MQTT e emular um teclado Bluetooth.

## ESP32 

![Descrição da Imagem](esp32.jpeg)


## Funcionalidades

- Conexão com Wi-Fi para obter o IP público.
- Atualização dos registros DNS no Cloudflare quando o IP público mudar.
- Conexão MQTT para receber e responder a comandos.
- Emulação de teclado Bluetooth para enviar teclas para um computador ou dispositivo.

## Componentes

- **ESP32**: Microcontrolador para gerenciar a conexão Wi-Fi, MQTT, Bluetooth e Cloudflare.
- **Cloudflare**: Usado para gerenciar registros DNS dinâmicos.
- **MQTT Broker**: Usado para comunicação em tempo real com o ESP32.
- **Bluetooth**: Emula um teclado Bluetooth para enviar entradas de teclas.

## Pré-requisitos

Para compilar e carregar o código para o seu ESP32, você precisa:

- **Placa ESP32**: Qualquer modelo baseado no ESP32.
- **Arduino IDE**: Usado para compilar e carregar o código no ESP32.
- **Bibliotecas Arduino**:
  - `WiFi.h` (para conexão Wi-Fi)
  - `PubSubClient.h` (para MQTT)
  - `HTTPClient.h` (para requisições HTTP)
  - `ArduinoJson.h` (para processar JSON)
  - `BleKeyboard.h` (para emular um teclado Bluetooth)

## Configuração

### 1. Configuração de Wi-Fi

No código, defina as credenciais do Wi-Fi:

```cpp
const char* ssid = "SEU_SSID";  // Wi-Fi SSID
const char* password = "SUA_SENHA"; // Wi-Fi Password
```

### 2. Configuração do MQTT

Configure o servidor MQTT e os tópicos de envio/recebimento:

```cpp
const char* mqtt_server = "IP_DO_SERVIDOR_MQTT"; // MQTT Server
const char* topic_request = "esp32/request"; // Tópico para comandos recebidos
const char* topic_response = "esp32/response"; // Tópico para respostas enviadas
```

### 3. Configuração do Cloudflare

Crie um token de API no Cloudflare e obtenha o Zone ID e os IDs dos registros DNS que você deseja atualizar:

```cpp
const char* token = "SEU_TOKEN"; // Token da API do Cloudflare
const char* zone_id = "SEU_ZONE_ID"; // Zone ID do Cloudflare
const char* records[] = {"ID_REGISTRO_1", "ID_REGISTRO_2"}; // IDs dos registros DNS
const int num_records = 2; // Número de registros DNS a serem atualizados
```

### 4. Conexão MQTT

Certifique-se de que você tem um servidor MQTT funcionando e configure-o para se conectar ao ESP32.

### 5. Bluetooth

Este código emula um teclado Bluetooth. Certifique-se de que o dispositivo está configurado para receber teclas do ESP32.

## Como usar

1. Compile e carregue o código no ESP32 usando o Arduino IDE.
2. O ESP32 se conectará automaticamente à rede Wi-Fi configurada.
3. O ESP32 buscará o seu IP público e, se ele mudar, atualizará os registros DNS no Cloudflare.
4. O ESP32 se conectará ao servidor MQTT e aguardará comandos.
5. Os comandos MQTT disponíveis:
    - **"IP"**: Retorna o IP local do ESP32.
    - **"REBOOT"**: Reinicia o ESP32.

Além disso, o ESP32 emulará um teclado Bluetooth com o nome "ESP32_Keyboard", permitindo que você envie entradas de teclado para outros dispositivos Bluetooth.

## Problemas conhecidos

- A API do Cloudflare pode ter limitações de taxa se você atualizar os registros DNS com muita frequência.
- A conexão com o Wi-Fi pode demorar alguns segundos, dependendo da sua rede.

## Licença

Este projeto está licenciado sob a Licença MIT. Veja o arquivo [LICENSE](LICENSE) para mais detalhes.

## Contribuições

Contribuições são bem-vindas! Se você encontrar algum bug ou quiser adicionar funcionalidades, sinta-se à vontade para abrir um issue ou um pull request.