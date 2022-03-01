/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "ISM43362Interface.h"

#include "TCPSocket.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

#include "HTS221Sensor.h"
#include "LPS22HBSensor.h"
#include <cstdio>

#define WIFI_SSID "WiFiSSID"
#define WIFI_PASSWORD "WiFiPassword"

#define WIFI_SECURITY NSAPI_SECURITY_WPA_WPA2

#define MQTT_HOST "demo.thingsboard.io"
#define MQTT_PORT 1883
#define MQTT_TOPIC "v1/devices/me/telemetry"

#define MAX_CHARACTERS_MSG 256

#define SAMPLE_RATE       500ms

class MQTTNetwork
{
public:
    MQTTNetwork(NetworkInterface* aNetwork) : network(aNetwork) {
        socket = new TCPSocket();
    }

    ~MQTTNetwork() {
        delete socket;
    }

    int read(unsigned char* buffer, int len, int timeout) {
        return socket -> recv(buffer, len);
    }

    int write(unsigned char* buffer, int len, int timeout) {
        return socket -> send(buffer, len);
    }

    int connect(const char* hostname, int port) {
        socket->open(network);

        SocketAddress socketAddr;
        network->gethostbyname(hostname, &socketAddr);
        socketAddr.set_port(port);
        
        printf("IP address is: %s\n", socketAddr.get_ip_address() ? socketAddr.get_ip_address() : "None");

        return socket -> connect(socketAddr);
    }

    int disconnect() {
        return socket -> close();
    }

private:
    NetworkInterface* network;
    TCPSocket* socket;
};

int main()
{
    // Initialise the digital pin LED1 as an output
    DigitalOut led(LED1);

    int count = 0;

    ISM43362Interface wifi(false);

    //Scan wifi networks
    WiFiAccessPoint *ap;

    count = wifi.scan(NULL, 0);
    printf("\n %d networks available. \n", count);

    //Limit number of network to aribitrary value of 15
    count = count < 15 ? count : 15;

    ap = new WiFiAccessPoint[count];
    count = wifi.scan(ap, count);
    for(int i = 0; i < count; i++)
    {
        printf("Network: %s RSSI: %hhd\n", ap[i].get_ssid(), ap[i].get_rssi());
    }

    delete[] ap;

    //Connecting to WiFi network

    printf("\nConnecting to %s... \n", WIFI_SSID);
    int ret = wifi.connect(WIFI_SSID, WIFI_PASSWORD, WIFI_SECURITY);
    if (ret != 0)
    {
        printf("\nConnection error\n");
        return -1;
    }

    printf("Success\n\n");
    printf("MAC: %s\n", wifi.get_mac_address());
    printf("IP: %s\n", wifi.get_ip_address());
    printf("Netmask: %s\n", wifi.get_netmask());
    printf("Gateway: %s\n", wifi.get_gateway());
    printf("RSSI: %d\n",  wifi.get_rssi());

    //Connect to ThingsBoard

    MQTTNetwork network(&wifi);
    MQTT::Client<MQTTNetwork, Countdown> client(network);

    char access_token[] = "your access token";

    MQTTPacket_connectData conn_data = MQTTPacket_connectData_initializer;
    conn_data.username.cstring = access_token;

    if (network.connect(MQTT_HOST, MQTT_PORT) < 0)
    {
        printf("failed to connect to " MQTT_HOST "\n");
        return -1;
    }

    if (client.connect(conn_data) < 0 ) 
    {
        printf("failed to send MQTT connect message \n");
        return -1;
    }

    printf("succcessfully connected! \n");

    //Initialise sensors
    uint8_t hum_tempID, pressureID; 
    DevI2C i2c_2(PB_11, PB_10);
    HTS221Sensor hum_temp(&i2c_2);
    LPS22HBSensor pressure(&i2c_2);

    pressure.init(NULL);
    pressure.enable();
    pressure.read_id(&pressureID);

    hum_temp.init(NULL);
    hum_temp.enable();
    hum_temp.read_id(&hum_tempID);

    printf("HTS221 humidity and temperature sensor = 0x%X\r\n", hum_tempID);
    printf("LPS22HB pressure sensor = 0x%X\r\n", pressureID);

    //yoink data
    while(1)
    {
        float temp, humidity, pressureValue;

        hum_temp.get_temperature(&temp);
        hum_temp.get_humidity(&humidity);

        pressure.get_pressure(&pressureValue);

        printf("HTS221:  [temp] %.2f C, [hum] %.2f%% [pressure] %.2f hPA\r\n", temp, humidity, pressureValue);

        char msg[MAX_CHARACTERS_MSG];
        int n = snprintf(msg, sizeof(msg), "{\"Temperature\":%.2f, \"humidity\":%.2f, \"Pressure\":%.2f}", temp, humidity, pressureValue);

        void *payload  = reinterpret_cast<void*>(msg);
        size_t payload_len = n;

        printf("publishing to: %s %d %s\r\n", MQTT_HOST, MQTT_PORT, MQTT_TOPIC);

        if (client.publish(MQTT_TOPIC, payload, n) < 0)
        {
            printf("Failed to publish MQTT Message :(\n");
        }    

        ThisThread::sleep_for(SAMPLE_RATE);

        //If not connected to a serial port should see LED turn on and off to indicate it is working correctly
        led = !led;
        printf("Blinking!!! \n");
    }

    //Should not get here

   /* while (true) {
        led = !led;
        ThisThread::sleep_for(BLINKING_RATE);
        printf("Blinking!!! \n");

    }*/

    client.disconnect();
    wifi.disconnect();

    printf("\n done \n");
    return 0;

}
