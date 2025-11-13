#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "am2315.h"
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <json-c/json.h>
#include "MQTTClient.h"
#include <wiringPiI2C.h>
#include <wiringPi.h>

bool is_service = false;
bool use_aht = true;
bool debug = false;
bool target = false;
bool aht_calibrated = false;

#define LED_pin 29 // raspberry pin 40	//GPIO21	// wiring pi pin 29 // https://i.stack.imgur.com/pdHum.jpg
#define BUZZER_pin 28 //rpi pin 38 //gpio20
#define ALARM_pin 25 //rpi pin 37 //gpio26
#define am_address 0x5c
#define bh_address 0x23
#define CONFIGFILE "config.json"

#define AHT20_I2CADDR 0x38
#define AHT20_CMD_SOFTRESET 0xBE

static unsigned char AHT20_CMD_INITIALIZE[3] = {0xBE, 0x08, 0x00};
static unsigned char AHT20_CMD_MEASURE[3] = {0xAC, 0x33, 0x00};

#define AHT20_STATUSBIT_BUSY 7       // The 7th bit is the Busy indication bit. 1 = Busy, 0 = not.
#define AHT20_STATUSBIT_CALIBRATED 3 // The 3rd bit is the CAL (calibration) Enable bit. 1 = Calibrated, 0 = not

#define QOS 1
#define TIMEOUT 10000L

MQTTClient client;

struct app_config
{
    const char *host;
    int port;
    const char *clientid;
    const char *username;
    const char *password;
    const char *pubtopic;
    const char *subtopic;
    int interval;
    int luxoffset;
    float tempoffset;
    int humoffset;
    int pm25offset;
    int tempminlimit;
    int tempmaxlimit;
    int humminlimit;
    int hummaxlimit;
    int buzzerenabled;
    int starthour;
    int startminute;
    int endhour;
    int endminute;
    int maxbuzzerduration;
};

struct app_config config = {.host = "", .port = 0, .clientid = "", .username = "", .password = "", .pubtopic = "", .subtopic = "", .interval = 0, .luxoffset = 0, .tempoffset = 0, .humoffset = 0, .pm25offset = 0, .tempmaxlimit = 35, .tempminlimit = 20, .hummaxlimit = 80, .humminlimit = 20, .buzzerenabled = 1, .starthour = 99, .startminute = 99, .endhour = 99, .endminute = 99, .maxbuzzerduration = 0};

volatile MQTTClient_deliveryToken deliveredtoken;
int ch;

bool get_normalized_bit(int value, int bit_index)
{
    // if (debug)
    // {
    //     if (!is_service)
    //     {
    //         int n = value;
    //         printf("normalize :");
    //         // array to store binary number
    //         int binaryNum[32];

    //         // counter for binary array
    //         int i = 0;
    //         while (n > 0)
    //         {
    //             // storing remainder in binary array
    //             binaryNum[i] = n % 2;
    //             n = n / 2;
    //             i++;
    //         }

    //         // printing binary array in reverse order
    //         for (int j = i - 1; j >= 0; j--)
    //         {
    //             printf("%d", binaryNum[j]);
    //         }

    //         printf(" @ %d \n", bit_index);
    //     }
    // }
    // Return only one bit from value indicated in bit_index
    int ch_Val = (value >> bit_index) & 1;
    // printf("check value  %d \n", ch_Val);

    if (ch_Val == 1)
        return true;
    return false;
}

bool cmd_soft_reset(int handle)
{
    // Send the command to soft reset
    int i = wiringPiI2CWriteReg8(handle, 0x0, AHT20_CMD_SOFTRESET);
    // if (debug)
    //     if (!is_service)
    //         printf("resetin device i: %d \n", i);
    sleep(0.04); // Wait 40 ms after poweron
    return i > 0;
}

int get_status(int handle)
{
    // Get the full status byte
    return wiringPiI2CRead(handle);
}

bool get_status_calibrated(int handle)
{
    // Get the calibrated bit
    wiringPiI2CWriteReg8(handle, 0x0, 0x71);
    return get_normalized_bit(get_status(handle), AHT20_STATUSBIT_CALIBRATED);
}

bool cmd_initialize(int handle)
{
    // Send the command to initialize (calibrate)
    int j = 0;
    j = write(handle, AHT20_CMD_INITIALIZE, 3);
    // j += wiringPiI2CWrite(handle, 0xBE);
    // j += wiringPiI2CWrite(handle, 0x08);
    // j += wiringPiI2CWrite(handle, 0x00);

    return j > 0;
}

int AHT20_inti()
{
    int ah = wiringPiI2CSetup(AHT20_I2CADDR);
    // if (debug)
    //     if (!is_service)
    //         printf("device handle: %d \n", ah);

    cmd_soft_reset(ah);
    sleep(0.1);

    if (!get_status_calibrated(ah)) //#Check for calibration, if not done then do and wait 10 ms
    {
        // if (debug)
        //     if (!is_service)
        //         printf("device not calibratede \n");
        cmd_initialize(ah);
        while (!get_status_calibrated(ah))
        {
            // if (debug)
            //     if (!is_service)
            //         printf("waiting for deviece to calibrate \n");
            sleep(0.01);
        }
    }

    return ah;
}

bool cmd_measure(int handle)
{
    // Send the command to measure
    int j = 0;
    j = write(handle, AHT20_CMD_MEASURE, 3);
    //     j += wiringPiI2CWrite(handle, 0xAC);
    // j += wiringPiI2CWrite(handle, 0x33);
    // j += wiringPiI2CWrite(handle, 0x00);

    sleep(0.08); // Wait 80 ms after measure
    // if (debug)
    //     if (!is_service)
    //         printf("wrote measure command : %d \n", j);
    return j > 0;
}

bool get_status_busy(int handle)
{
    // Get the busy bit
    return get_normalized_bit(get_status(handle), AHT20_STATUSBIT_BUSY);
}

void get_measure(int handle, float *tempe, float *humid)
{
    // Get the full measure

    // Command a measure
    cmd_measure(handle);

    // Check if busy bit = 0, otherwise wait 80 ms and retry
    while (get_status_busy(handle) == true)
        sleep(0.04); // Wait 80 ns

    // Read data and return it
    unsigned char data[7];
    read(handle, &data, 7);
    // unsigned char bytes;
    if (debug)
        if (!is_service)
        {
            printf("aht data: \n---\n");
            for (int i = 0; i < 7; i++)
            {
                printf(" byte int : %d /n", data[i]);
            }
            printf(" \n---\n");
        }
    // for (int i = 0; i < 7; i++)
    // {
    //     int dat = wiringPiI2CRead(handle);
    //     //int dat = wiringPiI2CReadReg8(handle, i);
    //     printf(" byte int : %d", dat);

    //     bytes = dat & 0xFF;

    //     if (debug)
    //         if (!is_service)
    //             printf("    | byte byte: %x", bytes);

    // data[i] = (char)dat;

    //     if (debug)
    //         if (!is_service)
    //             printf("  |  byte char : %c \n", data[i]);
    // }

    unsigned long __temp = 0;
    unsigned long __humi = 0;

    __temp = data[3] & 0x0f;
    __temp <<= 8;
    __temp += data[4];
    __temp <<= 8;
    __temp += data[5];

    float Temp = (float)__temp / 1048576.0 * 200.0 - 50.0;

    if (debug)
        if (!is_service)
            printf("aht20 temp:\n---\n%f\n---\n", Temp);

    *tempe = Temp;

    __humi = data[1];
    __humi <<= 8;
    __humi += data[2];
    __humi <<= 4;
    __humi += data[3] >> 4;

    // *humid = (float)__humi / 1048576.0;
    *humid = (float)__humi / 10485.0;
    if (debug)
        if (!is_service)
            printf("aht20 hum:\n---\n%f\n---\n", *humid);
}

static void writeconfig(char *line)
{
    FILE *fptr;
    fptr = fopen(CONFIGFILE, "w");
    fwrite(line, sizeof(char), strlen(line), fptr);
    fclose(fptr);
    
}

int loadconfig()
{
    char *json;
    int fd;
    struct json_object *obj;
    struct stat sb;
    long long int sz;

    int Result = stat(CONFIGFILE, &sb);
    if ((Result != 0) || (sb.st_mode & S_IFDIR))
    {
        return 1;
    }
    sz = sb.st_size;
    if (debug)
        if (!is_service)
            printf("File size: 0x%016llx\n", sz);

    fd = open(CONFIGFILE, O_RDONLY);
    json = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (debug)
        if (!is_service)
            printf("data from file:\n---\n%s\n---\n", json);
    close(fd);

    obj = json_tokener_parse(json);
    if (debug)
        if (!is_service)
            printf("jobj from file:\n---\n%s\n---\n", json_object_to_json_string_ext(obj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));
    int i = 0;
    json_object_object_foreach(obj, key, val)
    {
        if (debug)
            if (!is_service)
                printf("key = %s value = %s\n", key, json_object_get_string(val));

        if (strcmp(key, "username") == 0)
        {
            config.username = (char *)json_object_get_string(val);
            if (!is_service)
                printf("mqtt username: \"%s\" \n", config.username);
        }
        else if (strcmp(key, "password") == 0)
        {
            config.password = (char *)json_object_get_string(val);
            if (!is_service)
                printf("mqtt password: \"%s\" \n", config.password);
        }
        else if (strcmp(key, "host") == 0)
        {
            config.host = (char *)json_object_get_string(val);
            if (!is_service)
                printf("mqtt host: \"%s\" \n", config.host);
        }
        else if (strcmp(key, "port") == 0)
        {
            config.port = (int)json_object_get_int(val);
            if (!is_service)
                printf("mqtt port: \"%d\" \n", config.port);
        }
        else if (strcmp(key, "clientid") == 0)
        {

            config.clientid = (char *)json_object_get_string(val);
            if (!is_service)
                printf("mqtt clientid: \"%s\" \n", config.clientid);
            char *ptemp = (char *)malloc(100 * sizeof(char));
            char *stemp = (char *)malloc(100 * sizeof(char));
            sprintf(ptemp, "mqtt/telemetry/%s", config.clientid);
            sprintf(stemp, "mqtt/telemetry/%s/setting", config.clientid);
            config.pubtopic = ptemp;
            config.subtopic = stemp;
            if (!is_service)
                printf("mqtt pubtopic: \"%s\" \n", config.pubtopic);
            if (!is_service)
                printf("mqtt subtopic: \"%s\" \n", config.subtopic);
        }
        // else if (strcmp(key, "pubtopic") == 0)
        // {

        //     config.pubtopic = (char *)json_object_get_string(val);
        //     printf("mqtt pubtopic: \"%s\" \n", config.pubtopic);
        // }
        // else if (strcmp(key, "subtopic") == 0)
        // {

        //     config.subtopic = (char *)json_object_get_string(val);
        //     printf("mqtt subtopic: \"%s\" \n", config.subtopic);
        // }
        else if (strcmp(key, "interval") == 0)
        {
            config.interval = (int)json_object_get_int(val);
            if (!is_service)
                printf("publish interval: \"%d\" \n", config.interval);
        }
        else if (strcmp(key, "luxoffset") == 0)
        {
            config.luxoffset = (int)json_object_get_int(val);
            if (!is_service)
                printf("luxoffset: \"%d\" \n", config.luxoffset);
        }
        else if (strcmp(key, "tempoffset") == 0)
        {
            // printf("tempoffset as string: \"%s\" \n", (char *)json_object_get_string(val));
            // printf("tempoffset as float: \"%f\" \n", atof((char *)json_object_get_string(val)));
            config.tempoffset = atof((char *)json_object_get_string(val));
            if (!is_service)
                printf("tempoffset: \"%f\" \n", config.tempoffset);
        }
        else if (strcmp(key, "humoffset") == 0)
        {
            config.humoffset = (int)json_object_get_int(val);
            if (!is_service)
                printf("humoffset: \"%d\" \n", config.humoffset);
        }
        else if (strcmp(key, "pm25offset") == 0)
        {
            config.pm25offset = (int)json_object_get_int(val);
            if (!is_service)
                printf("pm25offset: \"%d\" \n", config.pm25offset);
        }
        else if (strcmp(key, "tempmaxlimit") == 0)
        {
            config.tempmaxlimit = (int)json_object_get_int(val);
            if (!is_service)
                printf("tempmaxlimit: \"%d\" \n", config.tempmaxlimit);
        }
        else if (strcmp(key, "tempminlimit") == 0)
        {
            config.tempminlimit = (int)json_object_get_int(val);
            if (!is_service)
                printf("tempminlimit: \"%d\" \n", config.tempminlimit);
        }
        else if (strcmp(key, "hummaxlimit") == 0)
        {
            config.hummaxlimit = (int)json_object_get_int(val);
            if (!is_service)
                printf("hummaxlimit: \"%d\" \n", config.hummaxlimit);
        }
        else if (strcmp(key, "humminlimit") == 0)
        {
            config.humminlimit = (int)json_object_get_int(val);
            if (!is_service)
                printf("humminlimit: \"%d\" \n", config.humminlimit);
        }        
        else if (strcmp(key, "buzzerenabled") == 0)
        {
            config.buzzerenabled = (int)json_object_get_int(val);
            if (!is_service)
                printf("buzzerenabled: \"%d\" \n", config.buzzerenabled);
        }
        
        else if (strcmp(key, "starthour") == 0)
        {
            config.starthour = (int)json_object_get_int(val);
            if (!is_service)
                printf("starthour: \"%d\" \n", config.starthour);
        }
        else if (strcmp(key, "startminute") == 0)
        {
            config.startminute = (int)json_object_get_int(val);
            if (!is_service)
                printf("startminute: \"%d\" \n", config.startminute);
        }
        else if (strcmp(key, "endhour") == 0)
        {
            config.endhour = (int)json_object_get_int(val);
            if (!is_service)
                printf("endhour: \"%d\" \n", config.endhour);
        }
        else if (strcmp(key, "endminute") == 0)
        {
            config.endminute = (int)json_object_get_int(val);
            if (!is_service)
                printf("endminute: \"%d\" \n", config.endminute);
        }
        else if (strcmp(key, "maxbuzzerduration") == 0)
        {
            config.maxbuzzerduration = (int)json_object_get_int(val);
            if (!is_service)
                printf("maxbuzzerduration: \"%d\" \n", config.maxbuzzerduration);
        }
        else
        {
            if (!is_service)
                printf("unknown key: \"%s\", val: \"%s\" \n", key, (char *)json_object_get_string(val));
        }
        i++;
    }
    if (debug)
        if (!is_service)
            printf("enteries: \"%d\" \n", i);
    if (i == 20)
        return 0;
    return 1;
}

void writeStopBuzzerOnce(int stopbuzz){
    FILE *file = fopen("stopbuzzeronce.txt", "w"); // Open file in write mode
    if (file == NULL) {
        printf("Error opening file!\n");
    }
    
    // Write content to the file
    fprintf(file, "%d", stopbuzz);
    fclose(file); // Close the file
}
int readStopBuzzerOnce() {
    int stopbuzz;
    FILE *file = fopen("stopbuzzeronce.txt", "r"); // Open file in read mode
    if (file == NULL) {
        printf("Error opening file!\n");
        return -1;
    }
    
    // Read an integer from the file
    fscanf(file, "%d", &stopbuzz);
    fclose(file); // Close the file
    return stopbuzz;
}
#define TEMP_HISTORY 180
float tempHistory[TEMP_HISTORY] = {0};
int tempHistoryIndex = 0;

void addToTempHistory(float temperature) {
    tempHistory[tempHistoryIndex] = temperature;
    tempHistoryIndex = (tempHistoryIndex + 1) % TEMP_HISTORY;
}

float getAverageTemp() {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < TEMP_HISTORY; ++i) {
        if (tempHistory[i] != 0) {
            sum += tempHistory[i];
            count++;
        }
    }
    if (count == 0) return 0; // Avoid division by zero
    return sum / count;
}

#define HUM_HISTORY 180
float humHistory[HUM_HISTORY] = {0};
int humHistoryIndex = 0;

void addToHumHistory(float humidity) {
    humHistory[humHistoryIndex] = humidity;
    humHistoryIndex = (humHistoryIndex + 1) % HUM_HISTORY;
}

float getAverageHum() {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < HUM_HISTORY; ++i) {
        if (humHistory[i] != 0) {
            sum += humHistory[i];
            count++;
        }
    }
    if (count == 0) return 0; // Avoid division by zero
    return sum / count;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    int del, ho, po, lo, tempmin, tempmax,hummin, hummax, buzzeron, starthr, startmin, endhr, endmin, maxbuzzerd;
    float to;
    
    if (debug)
        if (!is_service)
            printf("Message arrived\n");
    if (debug)
        if (!is_service)
            printf("     topic: %s\n", topicName);
    if (debug)
        if (!is_service)
            printf("   message: %.*s\n", message->payloadlen, (char *)message->payload);
    char *msg = message->payload;
    printf("%s\n", msg);
    printf("%s\n", msg);
    if (msg[0] == 'Q' || msg[0] == 'q')
    {
        ch = 'q';
    }
    else if (msg[0] == '{')
    {
        

        struct json_object *jobj, *tmp;
        jobj = json_tokener_parse(msg);
        // printf("jobj from str:\n---\n%s\n---\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));
        printf("Message arrived after jobj\n");
        const char *json_str = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY);
    if (json_str != NULL) {
        // Print the JSON string
        printf("JSON Object:\n%s\n", json_str);
    } else {
        printf("Error converting JSON object to string\n");
    }
        json_object_object_foreach(jobj, key, val)
        {
            printf("Message arrived inside foreach\n");
            // printf("key: \"%s\", val: \"%s\" \n", key, (char *)json_object_get_string(val));

            if (strcmp(key, "messageId") == 0)
            {
                if (!is_service)
                    printf("recived mesage id \"%s\" \n", (char *)json_object_get_string(val));
            }
            else if (strcmp(key, "date") == 0)
            {
                if (!is_service)
                    printf("recived date \"%s\" \n", (char *)json_object_get_string(val));
            }
            else if (strcmp(key, "deviceid") == 0)
            {
                
                if (!is_service)
                    printf("intended Device id: %s | My ID : %s \n", (char *)json_object_get_string(val), config.clientid);
                printf("The config clientid is: %s\n", config.clientid);
                printf("The incoming id is: %s\n", (char *)json_object_get_string(val));
                if (strcmp((char *)json_object_get_string(val), config.clientid) == 0)
                {
                    target = true;
                }
                else
                {
                    target = false;
                }
                if (!is_service)
                    printf("message for me? \"%s\" \n", strcmp((char *)json_object_get_string(val), config.clientid) ? "NO" : "YES");
            }

            else if (strcmp(key, "operator") == 0)
            {
                if (!is_service)
                    printf("intention \"%s\" \n", (char *)json_object_get_string(val));
            }

            else if (strcmp(key, "interval") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                del = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
                printf("The itnerval is: %d\n", del);
            }

            else if (strcmp(key, "offset") == 0)
            {
                // json_object_object_get_ex(jobj, "offset", &offsetobj);

                // printf("jobj from str:\n---\n%s\n---\n", json_object_to_json_string_ext(offsetobj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));
                tmp = json_object_array_get_idx(val, 0);
                // printf("offsets = %s\n", json_object_to_json_string(tmp));

                json_object_object_foreach(tmp, key1, val1)
                {
                    // printf("key: \"%s\", val: \"%s\" \n", key1, (char *)json_object_get_string(val1));
                    if (strcmp(key1, "temp") == 0)
                    {
                        // // printf("temp offset \"%d\" \n", (int)json_object_get_int(val1));
                        // printf("tempoffset as string: \"%s\" \n", (char *)json_object_get_string(val1));
                        // printf("tempoffset as float: \"%f\" \n", atof((char *)json_object_get_string(val1)));
                        to = atof((char *)json_object_get_string(val1));                        
                        // printf("temp offset \"%d\" \n", config.tempoffset);
                    }

                    else if (strcmp(key1, "hum") == 0)
                    {
                        // printf("hum offset \"%d\" \n", (int)json_object_get_int(val1));
                        ho = (int)json_object_get_int(val1);
                        // printf("hum offset \"%d\" \n", config.humoffset);
                    }

                    else if (strcmp(key1, "pm25") == 0)
                    {
                        // printf("pm25 offset \"%d\" \n", (int)json_object_get_int(val1));
                        po = (int)json_object_get_int(val1);
                        // printf("pm25 offset \"%d\" \n", config.pm25offset);
                    }

                    else if (strcmp(key1, "lux") == 0)
                    {
                        // printf("lux offset \"%d\" \n", (int)json_object_get_int(val1));
                        lo = (int)json_object_get_int(val1);
                        // printf("lux offset \"%d\" \n", config.luxoffset);
                    }
                }
            }
            else if (strcmp(key, "buzzerenabled") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                buzzeron = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
            }
            else if (strcmp(key, "tempminlimit") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                tempmin = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
            }
            else if (strcmp(key, "tempmaxlimit") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                tempmax = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
            }
            else if (strcmp(key, "humminlimit") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                hummin = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
            }
            else if (strcmp(key, "hummaxlimit") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                hummax = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
            }
            else if (strcmp(key, "maxbuzzerduration") == 0)
            {
                // printf("update intervel set to \"%s\" \n", (char *)json_object_get_string(val));
                maxbuzzerd = (int)json_object_get_int(val);
                // printf("update intervel set to : \"%d\" \n", config.interval);
            }
            else if (strcmp(key, "starttime") == 0)
            {
                if (strcmp((char *)json_object_get_string(val), "") == 0){
                    starthr = 99;
                    startmin = 99;
                }else{
                    int starthourint;                    
                    char *startstring = json_object_get_string(val);
                    int length = strlen(startstring);
                    startstring[length - 3] = '\0';
                    printf("%s\n", startstring);
                    
                    char firstTwo[3]; // Array to store first two characters and null terminator
                    strncpy(firstTwo, startstring, 2);
                    firstTwo[2] = '\0'; // Null-terminate the string
                    starthourint = atoi(firstTwo);
                    starthr = starthourint;
                    char lastTwo[3];
                    int startminuteint;
                    lastTwo[0] = startstring[length - 5];
                    lastTwo[1] = startstring[length - 4];
                    lastTwo[2] = '\0'; // Null-terminate the string
                    printf("%s\n", lastTwo);
                    startminuteint = atoi(lastTwo);
                    startmin = startminuteint;
                    
                }                
            }
            else if (strcmp(key, "endtime") == 0)
            {
                if (strcmp((char *)json_object_get_string(val), "") == 0){
                    endhr = 99;
                    endmin = 99;
                }else{
                    int endhourint;
                    char *endstring = json_object_get_string(val);
                    int length = strlen(endstring);
                    endstring[length - 3] = '\0';
                    printf("%s\n", endstring);
                    
                    char firstTwo[3]; // Array to store first two characters and null terminator
                    strncpy(firstTwo, endstring, 2);
                    firstTwo[2] = '\0'; // Null-terminate the string
                    endhourint = atoi(firstTwo);
                    endhr = endhourint;
                    char lastTwo[3];
                    int endminuteint;
                    lastTwo[0] = endstring[length - 5];
                    lastTwo[1] = endstring[length - 4];
                    lastTwo[2] = '\0'; // Null-terminate the string
                    printf("%s\n", lastTwo);
                    endminuteint = atoi(lastTwo);
                    endmin = endminuteint;
                }
            }
            else if (strcmp(key, "stopbuzzeronce") == 0)
            {                   
                writeStopBuzzerOnce((int)json_object_get_int(val) == 1);                
            }
            else
            {
                if (!is_service)
                    printf("unknown key: \"%s\", val: \"%s\" \n", key, (char *)json_object_get_string(val));
            }
        }
        printf("The target is: %d\n", target);
        if (target)
        {
            config.interval = del;
            config.tempoffset = to;
            config.humoffset = ho;
            config.pm25offset = po;
            config.luxoffset = lo;
            config.buzzerenabled = buzzeron;
            config.tempmaxlimit = tempmax;
            config.tempminlimit = tempmin;
            config.hummaxlimit = hummax;
            config.humminlimit = hummin;
            config.starthour = starthr;
            config.endhour = endhr;
            config.startminute = startmin;
            config.endminute = endmin;
            config.maxbuzzerduration = maxbuzzerd;
            if (!is_service)
                printf("update intervel set to : \"%d\" \n", config.interval);
            if (!is_service)
                printf("temp offset \"%f\" \n", config.tempoffset);
            if (!is_service)
                printf("hum offset \"%d\" \n", config.humoffset);
            if (!is_service)
                printf("pm25 offset \"%d\" \n", config.pm25offset);
            if (!is_service)
                printf("lux offset \"%d\" \n", config.luxoffset);
            if (!is_service)
                printf("buzzer \"%d\" \n", config.buzzerenabled);
            if (!is_service)
                printf("max temp \"%d\" \n", config.tempmaxlimit);
            if (!is_service)
                printf("min temp \"%d\" \n", config.tempminlimit);
            if (!is_service)
                printf("max humidity \"%d\" \n", config.hummaxlimit);
            if (!is_service)
                printf("min humidity \"%d\" \n", config.humminlimit);
            if (!is_service)
                printf("start hour \"%d\" \n", config.starthour);
            if (!is_service)
                printf("start minute \"%d\" \n", config.startminute);
            if (!is_service)
                printf("end hour \"%d\" \n", config.endhour);
            if (!is_service)
                printf("end minute \"%d\" \n", config.endminute);
                
            json_object *new = json_object_new_object();
            json_object_object_add(new, "username", json_object_new_string(config.username));
            json_object_object_add(new, "password", json_object_new_string(config.password));
            json_object_object_add(new, "host", json_object_new_string(config.host));
            json_object_object_add(new, "port", json_object_new_int(config.port));
            json_object_object_add(new, "clientid", json_object_new_string(config.clientid));
            json_object_object_add(new, "interval", json_object_new_int(config.interval));
            json_object_object_add(new, "luxoffset", json_object_new_int(config.luxoffset));
            char buf[25];
            gcvt(config.tempoffset, 3, buf);
            json_object_object_add(new, "tempoffset", json_object_new_string(buf));
            json_object_object_add(new, "humoffset", json_object_new_int(config.humoffset));
            json_object_object_add(new, "pm25offset", json_object_new_int(config.pm25offset));
            json_object_object_add(new, "tempminlimit", json_object_new_int(config.tempminlimit));
            json_object_object_add(new, "tempmaxlimit", json_object_new_int(config.tempmaxlimit));
            json_object_object_add(new, "humminlimit", json_object_new_int(config.humminlimit));
            json_object_object_add(new, "hummaxlimit", json_object_new_int(config.hummaxlimit));
            if (config.buzzerenabled == 1){
                json_object_object_add(new, "buzzerenabled", json_object_new_string("1"));
            }else{
                json_object_object_add(new, "buzzerenabled", json_object_new_string("0"));
            }            
            json_object_object_add(new, "starthour", json_object_new_int(config.starthour));
            json_object_object_add(new, "startminute", json_object_new_int(config.startminute));
            json_object_object_add(new, "endhour", json_object_new_int(config.endhour));
            json_object_object_add(new, "endminute", json_object_new_int(config.endminute));
            json_object_object_add(new, "maxbuzzerduration", json_object_new_int(config.maxbuzzerduration));
            writeconfig((char *)json_object_get_string(new));
            
        }
    }
    else if (msg[0] == '?')
    {
        MQTTClient_message ipmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken iptok;
        int rc;
        char *IP = (char *)malloc(100 * sizeof(char));
        struct ifaddrs *ifAddrStruct = NULL;
        struct ifaddrs *ifa = NULL;
        void *tmpAddrPtr = NULL;

        getifaddrs(&ifAddrStruct);

        for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr)
            {
                continue;
            }
            if (ifa->ifa_addr->sa_family == AF_INET)
            { // check it is IP4
                // is a valid IP4 Address
                tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
                char addressBuffer[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
                sprintf(IP + strlen(IP), "%s IP Address %s\n", ifa->ifa_name, addressBuffer);
            }
        }
        if (ifAddrStruct != NULL)
            freeifaddrs(ifAddrStruct);

        ipmsg.payload = IP;
        ipmsg.payloadlen = (int)strlen(IP);
        ipmsg.qos = QOS;
        ipmsg.retained = 0;

        if ((rc = MQTTClient_publishMessage(client, config.subtopic, &ipmsg, &iptok)) != MQTTCLIENT_SUCCESS)
        {
            if (!is_service)
                printf("Failed to publish IP, return code %d\n", rc);
            exit(EXIT_FAILURE);
        }

        if (!is_service)
            printf("Published IP response : %d \n", rc);
        free(IP);
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    if (target)
    {
        exit(121);
    }
    target = false;
    return 1;
}

int main(int argc, char *argv[])
{
    wiringPiSetup();
    pinMode(LED_pin, OUTPUT);
    pinMode(BUZZER_pin, OUTPUT);
    digitalWrite(BUZZER_pin, HIGH);
    pinMode(ALARM_pin, OUTPUT);
    digitalWrite(ALARM_pin, HIGH);
    if (argc >= 2)
    {
        for (int i = 0; i < argc; i++)
        {
            if (strcmp(argv[i], "-service") == 0)
            {
                is_service = true;
            }

            if (strcmp(argv[i], "-am") == 0)
            {
                printf("using AM2315 sensor instead of AM2315C\n");
                use_aht = false;
            }
        }
    }

    if (loadconfig() > 0)
    {
        if (!is_service)
            printf("Failed to read config file\n");
        return 0;
    }
    if (!is_service)
        printf("config file loaded\n");
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    char *url = (char *)malloc(50 * sizeof(char));
    sprintf(url, "tcp://%s:%d", config.host, config.port);
    if (!is_service)
        printf("[MQTT URL] %s\n", url);

    if ((rc = MQTTClient_create(&client, url, config.clientid,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
    {
        if (!is_service)
            printf("Failed to create client, return code %d\n", rc);
        rc = EXIT_FAILURE;
        goto exit;
    }

    if ((rc = MQTTClient_setCallbacks(client, NULL, NULL, msgarrvd, NULL)) != MQTTCLIENT_SUCCESS)
    {
        if (!is_service)
            printf("Failed to set callbacks, return code %d\n", rc);
        rc = EXIT_FAILURE;
        goto destroy_exit;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = config.username;
    conn_opts.password = config.password;
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        if (!is_service)
            printf("Failed to connect, return code %d\n", rc);
        rc = EXIT_FAILURE;
        goto destroy_exit;
    }

    printf("Subscribing to topic %s\nfor client %s using QoS%d\n",
           config.subtopic, config.clientid, QOS);
    if ((rc = MQTTClient_subscribe(client, config.subtopic, QOS)) != MQTTCLIENT_SUCCESS)
    {
        if (!is_service)
            printf("Failed to subscribe, return code %d\n", rc);
        rc = EXIT_FAILURE;
    }
    else
    {
        digitalWrite(LED_pin, LOW);
        char *i2c_device = "/dev/i2c-1";
        int aht = -1;
        void *am;
        if (use_aht)
        {
            aht = AHT20_inti();
        }
        else
        {
            am = am2315_init(am_address, i2c_device);
        }
        int bh = wiringPiI2CSetup(bh_address);
        wiringPiI2CWrite(bh, 0x01);
        char *PAYLOAD = (char *)malloc(250 * sizeof(char));
        const char *s;
        struct json_object *jobj;
        if (use_aht)
        {
            if (aht == -1)
            {
                printf("failed to initialize am2315C \n");
                return 1;
            }
        }
        else
        {
            if (am == NULL)
            {
                if (!is_service)
                    printf("failed to initialize am2315 \n");
                return 1;
            }
        }

        float temperature = 0, humidity = 0, pm25 = 0;
        int i = 0;       
        
        
        int buzzerBuzzing = 0;
        int buzzerDuration = 20;
        time_t buzzerStartTime = time(NULL);
        time_t nextMinBuzzTime = time(NULL);

        // Get the current time
        time_t now = time(NULL);
        time_t lastExecutionTime = time(NULL);
        time_t lastCheckTempTime = time(NULL);
        // Add 10 years to the current time
        struct tm *maxBuzzTimeStruct = localtime(&now);
        maxBuzzTimeStruct->tm_year += 10;
        time_t maxBuzzTime = mktime(maxBuzzTimeStruct);
        int addedMaxBuzzMinute = 0;
        int firstsend = 1;
        int buzzer_state = 0;
        do
        {
            // Get the current time
            time_t currentTime = time(NULL);
            // Calculate the time elapsed since the last execution
            int elapsedTime = (int)difftime(currentTime, lastExecutionTime);
            
            //Trigger BUZZER IF TEMP OVER LIMIT FOR 180 SEC
            // Calculate the time elapsed since the last execution
            int tempElapsedTime = (int)difftime(currentTime, lastCheckTempTime);

            // Check if the specified interval has elapsed
            if (tempElapsedTime >= 1) {
                
                addToTempHistory(temperature + config.tempoffset);
                float avgTemp = getAverageTemp();
                addToHumHistory(humidity + config.humoffset);
                float avgHum = getAverageHum();
                
                // Check if the buzzer is active
                if (buzzerBuzzing) {                    
                    if (config.buzzerenabled == 0) {
                        digitalWrite(BUZZER_pin, HIGH);
                        buzzerBuzzing = 0;
                        buzzer_state = 0;
                    }else if (readStopBuzzerOnce() == 1){
                        
                        digitalWrite(BUZZER_pin, HIGH);
                        buzzerBuzzing = 0;
                        buzzer_state = 0;
                    }
                    else if (maxBuzzTime < currentTime){
                        digitalWrite(LED_pin, HIGH);
                        sleep(1);
                        digitalWrite(LED_pin, LOW);
                        digitalWrite(BUZZER_pin, HIGH);
                        buzzerBuzzing = 0;
                        maxBuzzTime += 250000000;
                        addedMaxBuzzMinute = 0;
                        writeStopBuzzerOnce(1);
                        buzzer_state = 0;
                    }
                    else
                    {
                        // Calculate the elapsed time since the buzzer started
                        int buzzerElapsedTime = (int)difftime(currentTime, buzzerStartTime);
                        if (buzzerElapsedTime >= buzzerDuration) {
                            // Turn off the buzzer after the specified duration
                            digitalWrite(BUZZER_pin, HIGH);
                            buzzerBuzzing = 0;
                            buzzer_state = 1; //buzzer_state remain 1 because actually still buzzing
                        }
                    }
                                
                }else
                {
                    if (currentTime > nextMinBuzzTime){
                        // Check if the temperature or humidity exceeds the limits
                        if (!((avgTemp <= config.tempmaxlimit) && (avgTemp >= config.tempminlimit)) || !(((avgHum + config.humoffset) <= config.hummaxlimit) && ((avgHum + config.humoffset) >= config.humminlimit))) {
                            digitalWrite(ALARM_pin,LOW);
                            if (readStopBuzzerOnce() == 1){
                                buzzer_state = 0;
                                digitalWrite(BUZZER_pin, HIGH);
                                buzzerBuzzing = 0;   
                            } 
                            else {
                                if (config.buzzerenabled == 1) {
                                //99 means no configuration for time to off buzzer
                                if (config.starthour == 99 || config.startminute == 99 || config.endhour == 99 || config.endminute == 99){
                                    buzzer_state = 1;
                                    // Trigger the buzzer
                                    digitalWrite(BUZZER_pin, LOW);
                                    buzzerBuzzing = 1;
                                    buzzerStartTime = currentTime;
                                    nextMinBuzzTime = buzzerStartTime + buzzerDuration + 10;
                                }else{
                                    //check if the time is in range of non-buzz time
                                    //form time from int
                                   time_t start_time, end_time;                              
                                   struct tm *starttimeinfo;
                                   struct tm *endtimeinfo;

                                    // Convert current time to struct tm
                                    starttimeinfo = localtime(&currentTime);
                                    // Set configured hour and minute
                                    starttimeinfo->tm_hour = config.starthour;
                                    starttimeinfo->tm_min = config.startminute;
                                    // Convert struct tm back to time_t
                                    start_time = mktime(starttimeinfo);
                                    
                                    // Convert current time to struct tm
                                    endtimeinfo = localtime(&currentTime);
                                    // Set configured hour and minute
                                    endtimeinfo->tm_hour = config.endhour;
                                    endtimeinfo->tm_min = config.endminute;
                                    // Convert struct tm back to time_t
                                    end_time = mktime(endtimeinfo);

                                    if (currentTime < end_time && currentTime > start_time){
                                        //no buzz in this time
                                        buzzer_state = 0;
                                        digitalWrite(BUZZER_pin, HIGH);
                                        buzzerBuzzing = 0;                                        
                                    }else if (config.maxbuzzerduration > 0 && addedMaxBuzzMinute == 0){
                                             // Trigger the buzzer
                                            digitalWrite(BUZZER_pin, LOW);
                                            buzzer_state = 1;
                                            buzzerBuzzing = 1;
                                            buzzerStartTime = currentTime;
                                            nextMinBuzzTime = buzzerStartTime + buzzerDuration + 10;
                                            // Add to the current time, but check if added before or not
                                            maxBuzzTime = currentTime + (config.maxbuzzerduration*60);
                                            addedMaxBuzzMinute = 1;
                                        }                                    
                                    else                                    
                                    {                                        
                                         // Trigger the buzzer
                                        buzzer_state =1;
                                        digitalWrite(BUZZER_pin, LOW);
                                        buzzerBuzzing = 1;
                                        buzzerStartTime = currentTime;
                                        nextMinBuzzTime = buzzerStartTime + buzzerDuration + 10;
                                    }
                                    
                                }
                            }                     
                            }
                        } else {
                            digitalWrite(BUZZER_pin, HIGH);
                            digitalWrite(ALARM_pin, HIGH);
                            buzzerBuzzing = 0;
                            buzzer_state = 0;
                            if (readStopBuzzerOnce() == 1){
                                writeStopBuzzerOnce(0);
                            }                            
                        }
                    }
                    
                }
                // Update the last execution time
                lastCheckTempTime = currentTime;
            }
            
        // Check if the specified interval has elapsed
            if (elapsedTime >= config.interval || firstsend == 1) {
                // Update the last execution time
                lastExecutionTime = currentTime;
                firstsend = 0;
                
                
                // Your task to be executed on the specified interval here
                if (!is_service)
                printf("-------- %i ---------\n", i);
            if (use_aht)
            {
                get_measure(aht, &temperature, &humidity);
            }
            else
            {
                am2315_read_data(am, &temperature, &humidity);
            }

            int word = wiringPiI2CReadReg16(bh, 0x10);
            if (debug)
                if (!is_service)
                    printf("bh1750:\n---\n%d\n---\n", word);
            float lux = ((word & 0xff00) >> 8) | ((word & 0x00ff) << 8);
            // lux -= 22;
            if (lux == 65535)
                lux = 0;
            
            if (buzzer_state == 1){
                lux = 1;
            }else{
                lux = 0;
            }
            if (digitalRead(ALARM_pin) == HIGH){
                pm25 = 0;
            }else{
                pm25 = 1;
            }

            
            time_t t = time(NULL);
            struct tm tm = *localtime(&t);

            sprintf(PAYLOAD, ""
                             "{ \"messageId\":  %i , \
		\"date\": \"%02d-%02d-%02d %02d:%02d:%02d\",  \
		\"devicename\": \"%s\" , \
		\"operator\": \"update\" , \
    \"info\" : [ \
    { \
      \"temp\": \"%f\", \
      \"hum\": \"%f\", \
      \"pm25\": \"%f\", \
      \"lux\": \"%f\", \
    } \
    ] \
    }"
                             "",
                    i, tm.tm_mday, tm.tm_mon + 1, (tm.tm_year + 1900) % 100, tm.tm_hour, tm.tm_min, tm.tm_sec, config.clientid, temperature + config.tempoffset, humidity + config.humoffset, pm25 + config.pm25offset, lux + config.luxoffset);

            jobj = json_tokener_parse(PAYLOAD);
            s = json_object_get_string(jobj);

            if (!is_service)
                printf("json to publish:\n---\n%s\n---\n", s);
            strcpy(PAYLOAD, s);
            pubmsg.payload = PAYLOAD;
            pubmsg.payloadlen = (int)strlen(PAYLOAD);
            pubmsg.qos = QOS;
            pubmsg.retained = 0;

            printf("response %s \n", config.pubtopic);
            if ((rc = MQTTClient_publishMessage(client, config.pubtopic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS)
            {
                if (!is_service)
                    printf("Failed to publish message, return code %d\n", rc);
                exit(EXIT_FAILURE);
            }

            if (!is_service)
                printf("response %d \n", rc);

            rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
            digitalWrite(LED_pin, HIGH);
            delay(500);
            digitalWrite(LED_pin, LOW);
            
            i++;
            }
            
        } while (ch != 'Q' && ch != 'q');

        if ((rc = MQTTClient_unsubscribe(client, config.subtopic)) != MQTTCLIENT_SUCCESS)
        {
            if (!is_service)
                printf("Failed to unsubscribe, return code %d\n", rc);
            rc = EXIT_FAILURE;
        }
        am2315_close(am);
    }

    if ((rc = MQTTClient_disconnect(client, 10000)) != MQTTCLIENT_SUCCESS)
    {
        if (!is_service)
            printf("Failed to disconnect, return code %d\n", rc);
        rc = EXIT_FAILURE;
    }

destroy_exit:
    MQTTClient_destroy(&client);
exit:
    digitalWrite(LED_pin, HIGH);
    delay(1000);
    return rc;
}
